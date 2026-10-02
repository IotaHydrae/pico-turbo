# Troubleshooting a build that will not run

> Read the state line first: requested vs achieved clock, flash clock and regulator
> setting usually say whether the frequency, the voltage or the flash divider is wrong.

## TL;DR

- **Check what it actually got.** `pico_turbo_state()` reports requested/achieved
  `clk_sys`, `clk_peri`, `clk_usb`, `flash_clk_khz`, `vreg_sel`, `reached` and `hangs`.
- **A frequency the PLL cannot hit exactly is not instability.** The clock simply does not
  move and `reached` is false. Three minutes of soak is an expensive way to learn this.
- **Raise the flash divider** if it boots and then misbehaves: `-DPICO_TURBO_FLASH_CLK_DIV=6`
  or `10` (a divider must be even on RP2040).
- **Run from SRAM** when the flash genuinely cannot keep up: `pico_set_binary_type(app
  copy_to_ram)`, or `-DPICO_COPY_TO_RAM=1` for an SDK build that honours it.
- **Never leave a too-fast divider in boot2.** It re-runs on every reset and can leave a
  board that only the BOOTSEL button recovers.

## Order of checks

### 0. The state line

`pico_turbo_state()` is the first thing to read:

- `requested_khz != sys_clk_khz` (or `reached == false`): the PLL cannot produce that
  frequency exactly. Nothing is wrong with the chip; pick a reachable point
  (`check_sys_clock_khz()`), or let the search skip the unreachable step.
- `usb_ok == false`: `clk_usb` has left 48 MHz, which should not happen (it is a separate
  PLL) and means something else reconfigured it.
- `flash_clk_khz` high for the board: suspect the flash divider, not the core clock.

### 1. Lower the flash clock

The auto-derived divider keeps XIP under the board's `_FLASH_MAX_KHZ`. A board with a
slower flash part — or a clone sharing a board name — may need less:

```bash
cmake -DPICO_TURBO_FLASH_CLK_DIV=6 ..
cmake -DPICO_TURBO_FLASH_CLK_DIV=10 ..   # even on RP2040; see docs/flash-divider.md
```

Raising `PICO_TURBO_FLASH_MAX_KHZ` is only right when a measurement says the part takes
more; the failure mode of a divider that is too fast is wrong data read through XIP, not
an obvious crash.

### 2. Run from SRAM

When the flash cannot match the system clock at all, copy the firmware to SRAM at boot.
This removes the XIP bottleneck (at the cost of available RAM):

```cmake
pico_set_binary_type(your_app copy_to_ram)
```

or, for a build that reads the SDK's own switch, `-DPICO_COPY_TO_RAM=1`. The
`examples/benchmark` project has its own `-DCOPY_TO_RAM=ON` option that does the same for
that target.

### 3. It runs but computes wrong answers

That is the usual overclock failure, and a crash test does not catch it.
`pico_turbo_self_test(ms)` returns a hash of a fixed workload plus a region of the image
read back through XIP; compare it with a hash taken on a configuration known to be good
(`pico_turbo_self_check(ms, reference)`). A mismatch at a frequency the board *runs* at
means the frequency, the voltage or the flash timing is marginal.

### 4. It locks up and needs power cycling

Either the operating point needs more voltage than it was given, or the ordering was
bypassed. `pico_turbo_apply()` is the only supported path: raising does voltage → settle →
clock, lowering does clock → voltage. Feeding the regulator and the clock by hand in one
order locks the chip up at a low voltage.

## Safety

- A flash divider fast enough to break boot2 cannot be fixed by a software reset. Keep
  the BOOTSEL button reachable while probing dividers, and prefer verifying XIP with the
  self-check before trusting a new ceiling.
- 1.30 V (RP2040) / 1.60 V (RP2350) is the policy ceiling. A request above it is clamped,
  not refused — measured on a WeAct RP2350A, a 1.65 V request at 520 MHz left the app at
  the stock 150 MHz with the divider applied, and only the state line showed it.

## Related

- [design.md](design.md) — the ordering and state fields
- [flash-divider.md](flash-divider.md) — the divider and boot2
- [measurements/method-notes.md](measurements/method-notes.md) — traps that produce
  "unstable" reports which are not instability
