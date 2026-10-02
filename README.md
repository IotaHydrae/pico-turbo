# pico_turbo

[中文版本](README_zh.md)

> A lightweight overclocking library for Raspberry Pi Pico (RP2040 / RP2350): set a
> target clock, call one function, and the voltage, PLL and flash divider follow —
> safely, and only because the configuration was measured on a real board.

## TL;DR

- Add it as a submodule, set `PICO_TURBO_SYS_CLK_KHZ` (e.g. `400000`), call
  `pico_turbo_init()` first in `main()`. Without a target the library is a no-op.
- A board with a measured profile can carry a default: `-DPICO_BOARD=pico_w` alone
  applies 440 MHz at 1.30 V and says so at configure time. `-DPICO_TURBO_PROFILE=none`
  asks for stock clocks; an explicit profile or clock wins.
- `PICO_TURBO_AUTOTUNE=1` lets the chip find its own stable configuration at boot, with
  a watchdog to catch a candidate that hangs; `examples/tune` writes the result down as
  a per-chip tier header.
- Anything timed from `clk_sys`/`clk_peri` at compile time must be recomputed after the
  call. USB is the exception: it runs from its own 48 MHz PLL.
- Every default and ceiling here is a measurement. They are recorded, with the boards
  they came from, in [`docs/`](docs/README.md).

## Quick start

```bash
# Add to your project
git submodule add https://github.com/IotaHydrae/pico-turbo.git libs/pico-turbo
```

```cmake
# In your CMakeLists.txt
set(PICO_TURBO_SYS_CLK_KHZ 400000)        # target 400 MHz
add_subdirectory(libs/pico-turbo)
target_link_libraries(your_app pico_turbo)
```

Call `pico_sdk_init()` before `add_subdirectory(pico-turbo)`: the flash divider is
patched into the boot stage 2 there, and without the target the patch is skipped with a
warning.

```c
// In your main.c — call once before any peripheral init
#include <pico_turbo.h>

int main(void) {
    pico_turbo_init();
    // ...
}
```

## Auto-tuning

Silicon is not uniform: how far a part clocks depends on the die, the board, the flash
and the temperature. Turn the search on and let the chip answer it itself:

```cmake
set(PICO_TURBO_SYS_CLK_KHZ 125000)   # where the search starts (must be safe)
set(PICO_TURBO_AUTOTUNE 1)           # climb from there
set(PICO_TURBO_MAX_CLK_KHZ 420000)   # up to here
```

`pico_turbo_init()` walks the ladder in `PICO_TURBO_STEP_KHZ` steps, raising the voltage
when a step fails and keeping the last configuration that passed. Steps the PLL cannot
produce exactly are skipped, not rounded. A candidate that hangs is caught by the
watchdog and resumed one voltage step up on the next boot; a warm reset reuses the stored
result. `pico_turbo_state()` reports what was found and `pico_turbo_trace()` lists every
configuration tried. Details: [docs/autotune.md](docs/autotune.md).

`examples/tune` runs the same search harder, re-verifies every tier, and prints a
`pico_turbo_config.h` between markers: a table of the stable configurations **measured on
that chip**, for `pico_turbo_use_table()` / `pico_turbo_select()`. The top tier is a
candidate, not a guarantee — a 30 ms screen accepted a frequency at which CoreMark could
not start. Accept it with a long, mixed workload.

## API

```c
/* policy: what the build asked for, or a search on demand */
void pico_turbo_init(void);
pico_turbo_config_t pico_turbo_autotune(const pico_turbo_autotune_t *policy);

/* mechanism: a frequency and a voltage of your own choosing */
bool pico_turbo_apply(const pico_turbo_config_t *config);
pico_turbo_state_t pico_turbo_state(void);
uint16_t pico_turbo_voltage_for_khz(uint32_t khz);

/* self-check: does the chip still produce the same answers? */
uint32_t pico_turbo_self_test(uint32_t ms);
bool pico_turbo_self_check(uint32_t ms, uint32_t reference);

/* reporting */
uint32_t pico_turbo_trace(const pico_turbo_step_t **steps);
```

`pico_turbo_apply()` is the mechanism on its own: voltage first when going up, clock
first when going down, a settle delay in between. A frequency the PLL cannot hit exactly
is left where it was rather than rounded; the return value says whether it was reached.
`pico_turbo_self_test(ms)` hashes a fixed self-checking workload plus a region of the
image read back through XIP — comparing it against a value from a trusted chip is how a
frequency, voltage or flash timing is judged, because a chip that runs and computes wrong
answers is the usual failure mode.

The header `include/pico_turbo.h` documents every field and return value.

## Documentation

| Document | Content |
| --- | --- |
| [docs/README.md](docs/README.md) | Knowledge-base index and maintenance conventions |
| [docs/configuration.md](docs/configuration.md) | Every build switch, board profile and resolved variable |
| [docs/design.md](docs/design.md) | How `init()`/`apply()` work, the safe ordering, what must follow the call |
| [docs/flash-divider.md](docs/flash-divider.md) | Flash divider derivation, the RP2040 even rule, the boot2 patch |
| [docs/autotune.md](docs/autotune.md) | The runtime search and the per-chip tier header |
| [docs/boards.md](docs/boards.md) | Board files, profiles and ceilings |
| [docs/troubleshooting.md](docs/troubleshooting.md) | A build that will not run, runs wrong, or locks up |
| [docs/testing.md](docs/testing.md) | Offline host tests, oracles, and the `tools/turbocfg.py` CLI |
| [docs/measurements.md](docs/measurements.md) | Index of the measurement records |

`examples/benchmark` is a small stock-vs-overclocked workload for comparing a build
(`-DCOPY_TO_RAM=ON` runs it from SRAM); `examples/tune` is the measuring tool above.

## Tests

The half of the library that is not silicon-dependent — the order the regulator and the
clock are touched in, the refusal of configurations that are not configurations, reducing
a search trace to a tier table, the bounds a search keeps to — is tested on the host,
with no board attached:

```bash
make -C test/host        # build and run; 0 = pass, 1 = fail
```

It also runs the offline tests for `tools/turbocfg.py`, the board-file/tier-table utility.
What remains — whether *this* RP2040 does 420 MHz — can only be answered on a chip.

## ⚠️ Warning

Overclocking increases power consumption and heat, and **may permanently damage your
device**. Use at your own risk. The library is provided as-is with no warranty.

## License

MIT — see [LICENSE](LICENSE).
