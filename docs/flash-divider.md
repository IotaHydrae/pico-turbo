# Flash divider

> The XIP clock is `clk_sys / PICO_FLASH_SPI_CLKDIV`, it is baked into boot stage 2
> before `main()`, and the safe ceiling is a property of the **board's flash part**, not
> of the RP2350's QSPI interface.

## TL;DR

- The divider is derived from the **highest frequency the build may reach**
  (`PICO_TURBO_MAX_CLK_KHZ`), not from the frequency it starts at: every search candidate
  must be safe, or the flash fails first.
- `_FLASH_MAX_KHZ` is per board: 57 MHz (`pico2`), 110 MHz (`pico_w`), 52 MHz
  (`weact_rp2350a`), 133 MHz when no board file carries one.
- **Even dividers are an RP2040 requirement.** RP2350's `w25q080` boot stage 2 (what the
  official boards build) checks only the maximum; its QMI divider has no parity
  constraint. Three Waveshare RP2350 boards ship `PICO_FLASH_SPI_CLKDIV 3`.
- A too-fast divider can brick a board past what software can recover: the divider lives
  in boot2, so every reset re-runs it.

## Arithmetic

```text
_DIV = ceil(PICO_TURBO_MAX_CLK_KHZ / PICO_TURBO_FLASH_MAX_KHZ)   # min 2
if board requires even and _DIV is odd: _DIV += 1
```

`PICO_TURBO_FLASH_CLK_DIV` given explicitly skips the derivation; it is then validated
(≥ 2, and parity when the board requires it) instead of adjusted.

The result is passed to the library as both the SDK's `PICO_FLASH_SPI_CLKDIV` and the
library's own `PICO_TURBO_FLASH_DIV` (the same number under a name another component
cannot redefine last), and to `pico_turbo_state().flash_clk_khz` as
`clk_sys / PICO_TURBO_FLASH_DIV`.

## Why the ceiling is per board

The QSPI interface's own limit is 133 MHz, which is not what a flash part or a board
layout tolerates. Measured on an official Pico 2, a 520 MHz build with the default
133 MHz ceiling derived DIV 4 — 130 MHz of flash clock at the top, 78 MHz at 315 MHz —
and the chip started failing its own self-check and taking hard faults. It looked exactly
like a silicon frequency limit. With DIV 10 (52 MHz) the same search climbed to 520 MHz,
its configured ceiling, without a single hang; running from SRAM instead of flash
(`-DPICO_COPY_TO_RAM=1`) gave the same result, which is what says the problem was the
flash clock and not instruction fetch.

Two RP2350A boards of the same chip revision bracket flash ceilings more than a factor of
two apart (Luckfox: wrong at 78.75 MHz, solid at 57; official Pico 2: solid at 109.2,
hard fault at 112.8). The ladder and its numbers are in
[measurements/flash-ceiling.md](measurements/flash-ceiling.md).

That is also why `pico2`'s ceiling is 57 MHz and not 60: with odd dividers allowed, a
60 MHz ceiling derives DIV 9 at 520 MHz = 57.8 MHz, which is **above** what the clone was
shown to hold. The even-only rule used to keep the flash clock just under the ceiling by
accident; with it off, the ceiling has to be the real limit, and clones share the
`pico2` board name.

The `weact_rp2350a` file carries 52 MHz because that is the fastest this board has been
*shown* to hold (DIV 10 at 520 MHz). Its ladder has not been run; the part is rated
133 MHz. With no board file at all, the library falls back to 133 MHz and derives DIV 4
at 520 MHz = 130 MHz — the divider this project has already measured as one a board does
not come back from without the button.

## The boot2 patch

The divider has to be right from the first XIP access, before `main()` runs, so it
belongs to boot stage 2. When the computed divider differs from the board's
`_BOOT2_DEFAULT_DIV`, the library sets `PICO_FLASH_SPI_CLKDIV` on the `bs2_default`
target. A **lower** divider matters too: leaving boot2 at 4 while the application uses 2
clocks the flash at half of what it could take.

Call `pico_sdk_init()` before `add_subdirectory(pico-turbo)`. If `bs2_default` does not
exist yet the patch is skipped and CMake warns that XIP will run at the board default.

## Overrides

```bash
cmake -DPICO_TURBO_FLASH_MAX_KHZ=55000 ..   # lower the ceiling
cmake -DPICO_TURBO_FLASH_CLK_DIV=10 ..      # pin the divider (even on RP2040)
cmake -D_FLASH_MAX_KHZ=60000 ..             # override the board file's value
```

## Related

- [measurements/flash-ceiling.md](measurements/flash-ceiling.md) — the divider ladders
- [design.md](design.md) — the ordering and state rules around this
- [boards.md](boards.md) — each board file's `_FLASH_MAX_KHZ` and parity rule
