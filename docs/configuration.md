# Build configuration

> `PICO_TURBO_SYS_CLK_KHZ` (or a board profile) is the whole of the normal configuration;
> everything else has a safe default and only exists to override a measurement.

## TL;DR

- Leave `PICO_TURBO_SYS_CLK_KHZ` empty → the library is a no-op and the project runs at
  stock clocks.
- A board file may supply a default profile: `-DPICO_BOARD=pico_w` alone applies that
  board's measured extreme (440 MHz, 1.30 V, DIV 4) and prints it at configure time.
- Precedence: explicit clock > explicit profile > board default > stock.
  `-DPICO_TURBO_PROFILE=none` asks for stock clocks and wins over the board default.
- After `add_subdirectory(pico-turbo)` the resolved values are in the caller's scope as
  `PICO_TURBO_RESOLVED_*`.

## Inputs

| Variable | Default | Meaning |
| --- | --- | --- |
| `PICO_TURBO_SYS_CLK_KHZ` | *(empty)* | Target `clk_sys` in kHz, e.g. `400000`. Empty = no overclock. With auto-tune this is the search's **start** frequency, which must itself be safe. |
| `PICO_TURBO_PROFILE` | *(empty)* | A profile from the board file (`safe` / `fast` / `turbo` / `extreme`, set varies per board; `none` = stock). An unknown name is a configure error. |
| `PICO_TURBO_VREG_VOLTAGE` | *(auto)* | Core voltage as a `VREG_VOLTAGE_*` register encoding, e.g. `VREG_VOLTAGE_1_25`. Empty = derive from the frequency table (see [design.md](design.md)). |
| `PICO_TURBO_FLASH_CLK_DIV` | *(auto)* | Flash divider; setting it stops the auto derivation. Must be ≥ 2, and even on a board whose boot stage 2 requires it. |
| `PICO_TURBO_FLASH_MAX_KHZ` | board's `_FLASH_MAX_KHZ`, else `133000` | Ceiling the automatic divider keeps the XIP clock under. Lower it for a slow flash part. |
| `PICO_TURBO_MAX_CLK_KHZ` | auto-tune: platform ceiling; fixed build: `PICO_TURBO_SYS_CLK_KHZ` | Highest frequency the build may apply. The flash divider is derived from this, not from the start frequency. |

## Auto-tune inputs

| Variable | Default | Meaning |
| --- | --- | --- |
| `PICO_TURBO_AUTOTUNE` | `0` | Run the runtime search instead of one fixed point. |
| `PICO_TURBO_MAX_CLK_KHZ` | platform ceiling | Top of the search. Must be ≥ the start frequency. |
| `PICO_TURBO_STEP_KHZ` | `5000` | Search step. Frequencies the PLL cannot hit exactly are skipped, not rounded. |
| `PICO_TURBO_STRESS_MS` | `30` | Stress time per candidate. Longer means fewer false positives and a slower boot. |

The search itself is described in [autotune.md](autotune.md). Its policy can also be
called at runtime with `pico_turbo_autotune(&policy)`; the `pico_turbo_autotune_t`
fields default to the compile-time values when left zero.

## Voltage availability

`VREG_VOLTAGE_DEFAULT`, `1_20`, `1_25`, `1_30` exist on RP2040. RP2350 additionally has
the extended range `1_35` … `3_30`, but this library's policy ceiling is
`PICO_TURBO_MAX_VREG_VOLTAGE` = 1.30 V on RP2040 and 1.60 V on RP2350
(`include/pico_turbo_internal.h`). A request above that ceiling is **clamped, not
refused**: measured on a WeAct RP2350A, asking for 1.65 V at 520 MHz left the
application at the stock 150 MHz with the flash divider applied — a silent wrong
result. See [measurements/clock-ceilings.md](measurements/clock-ceilings.md).

## Board profiles

A board file in `boards/` sets `_PLATFORM_MAX_KHZ`, `_FLASH_MAX_KHZ` (optional),
`_BOOT2_DEFAULT_DIV`, `_FLASH_REQUIRES_EVEN` and its profiles. `pico_w` is currently the
only file with a **default** profile. Per-board values and their provenance:
[boards.md](boards.md).

## Resolved values

Once the directory has been added, these are set in the caller's scope — including a
clock that came from a board profile, which `PICO_TURBO_SYS_CLK_KHZ` does not show:

| Variable | Meaning |
| --- | --- |
| `PICO_TURBO_RESOLVED_CLK_KHZ` | The clock the build resolved to (empty when no overclock). |
| `PICO_TURBO_RESOLVED_MAX_CLK_KHZ` | The highest the build may apply. |
| `PICO_TURBO_RESOLVED_FLASH_DIV` | The divider baked into boot stage 2. |
| `PICO_TURBO_RESOLVED_FLASH_CLK_KHZ` | `RESOLVED_MAX_CLK_KHZ / RESOLVED_FLASH_DIV`. |

A CoreMark port derived its iteration count from `PICO_TURBO_SYS_CLK_KHZ`, saw nothing
under a profile build, sized its work for 125 MHz while the chip ran at 420 MHz, and
produced a run under CoreMark's own ten-second rule. Read the resolved values.

## Command line

```bash
cmake -DPICO_TURBO_SYS_CLK_KHZ=400000 ..
cmake -DPICO_TURBO_SYS_CLK_KHZ=512000 -DPICO_TURBO_FLASH_CLK_DIV=10 ..
cmake -DPICO_TURBO_SYS_CLK_KHZ=400000 -DPICO_TURBO_VREG_VOLTAGE=VREG_VOLTAGE_1_25 ..
cmake -DPICO_TURBO_FLASH_MAX_KHZ=55000 ..          # override the board ceiling
cmake -D_PLATFORM_MAX_KHZ=440000 ..                # probe past the board ceiling
cmake -DPICO_BOARD=pico_w -DPICO_TURBO_PROFILE=none ..   # stock clocks
```

`PICO_TURBO_FLASH_MAX_KHZ` and the board file's `_PLATFORM_MAX_KHZ` / `_FLASH_MAX_KHZ`
are all overridable from the command line, which is how a board's real limits are probed
without editing the file.

## The tune example's own knobs

`examples/tune` exists to measure, not to run fast; it has separate knobs:

| Variable | Default | Meaning |
| --- | --- | --- |
| `TUNE_BASE_KHZ` | `125000` | Search start. Setting it equal to the ceiling makes a single-point test. |
| `TUNE_STEP_KHZ` | `5000` | Search step. |
| `TUNE_STRESS_MS` | `100` | Stress time per candidate. |
| `TUNE_MIN_VREG_SEL` / `TUNE_MAX_VREG_SEL` | *(empty)* | Voltage window, in `VREG_VOLTAGE_*` encodings (RP2350: 17 = 1.40 V). |
| `TUNE_MAX_HANGS` | library default (`PICO_TURBO_MAX_HANGS`, 2) | Watchdog resets one wall is worth. |
| `TUNE_VERIFY_MS` | `200` | Second soak each tier gets. **Together with `PICO_TURBO_STRESS_MS` this is what "passed" means.** |
| `TUNE_LEAVE_AT_MAX` | `0` | Leave the top tier running instead of the safest one. |

## Related

- [design.md](design.md) — what the configuration does to the chip
- [flash-divider.md](flash-divider.md) — the divider arithmetic and its per-board ceiling
- [boards.md](boards.md) — the values each board file carries
