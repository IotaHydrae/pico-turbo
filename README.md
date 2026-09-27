# pico_turbo

[中文版本](README_zh.md)

A lightweight overclocking library for Raspberry Pi Pico (RP2040 / RP2350).

Add it as a git submodule, set a target frequency, call one function — done.

## Quick Start

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

```c
// In your main.c — call once before any peripheral init
#include <pico_turbo.h>

int main(void) {
    pico_turbo_init();
    // ...
}
```

That's it. If you don't set `PICO_TURBO_SYS_CLK_KHZ` (or leave it empty), the library is a no-op — your project compiles and runs at stock clocks with zero overhead.

## Auto-tuning

Silicon is not uniform: how far a part clocks depends on the die, the board, the
flash and the temperature, so a frequency that is stable on one Pico can be
unreachable on the next.  Turn the search on and let the chip answer that question
itself:

```cmake
set(PICO_TURBO_SYS_CLK_KHZ 125000)   # where the search starts (must be safe)
set(PICO_TURBO_AUTOTUNE 1)           # climb from there
set(PICO_TURBO_MAX_CLK_KHZ 420000)   # up to here
```

`pico_turbo_init()` then walks the frequency ladder in `PICO_TURBO_STEP_KHZ` steps,
raising the core voltage when a step fails, stress-testing every candidate and
keeping the last configuration that passed.  Steps the PLL cannot produce exactly
are skipped rather than rounded.  A trace of everything it tried is available
afterwards:

```c
pico_turbo_state_t st = pico_turbo_state();
printf("%lu kHz at sel %u%s\n", (unsigned long)st.sys_clk_khz,
       (unsigned)st.vreg_sel, st.tuned ? " (searched)" : "");

const pico_turbo_step_t *steps;
for (uint32_t i = 0, n = pico_turbo_trace(&steps); i < n; i++) {
        printf("%lu kHz sel %u -> %u\n", (unsigned long)steps[i].khz,
               (unsigned)steps[i].vreg_sel, steps[i].result);
}
```

| Variable | Default | Description |
|---|---|---|
| `PICO_TURBO_AUTOTUNE` | `0` | Search for the best frequency and voltage instead of using one fixed point |
| `PICO_TURBO_MAX_CLK_KHZ` | platform maximum | Highest frequency the search may try |
| `PICO_TURBO_STEP_KHZ` | `5000` | Frequency step |
| `PICO_TURBO_STRESS_MS` | `30` | How long each candidate is stressed |

The search runs at the very start of `main()`, before anything else has happened,
for a reason: a candidate that *hangs* is caught by the watchdog, which resets the
chip and starts the program again -- the search then reads what it had written to
the watchdog scratch registers, records that candidate as hung, and comes back at
the best configuration that had already passed.  A chip that keeps hanging gives up
after `PICO_TURBO_MAX_HANGS` resets and stays at the starting frequency.  The
result is kept in those registers as well, so a warm reset reuses it instead of
searching again; a power cycle starts over.  The search can also be run later, with
your own policy: `pico_turbo_autotune(&policy)`.

## Tuned configurations: measuring one chip and writing it down

A search answers "what can this chip do" for the run it is in.  The `tune` example
answers it once and writes the answer down, as a C header with a table of usable
configurations:

```bash
export PICO_SDK_PATH=/path/to/pico-sdk
mkdir -p examples/tune/build && cd examples/tune/build
cmake .. && ninja          # -DPICO_BOARD=pico2 for a Pico 2
```

Flash it, open the serial console, and it searches, verifies every candidate again
at length, and prints the file between two markers:

```text
---8<--- BEGIN pico_turbo_config.h ---8<---
/*
 * pico_turbo: the stable configurations measured on this chip.
 * ...
 * board id : e6:63:...
 * measured : Sep 27 2025 14:52:01
 * tiers    : 4
 */
#define PICO_TURBO_TUNED_COUNT 4u
#define PICO_TURBO_TUNED_SAFE  0u  /* slowest, lowest voltage */
#define PICO_TURBO_TUNED_MAX   3u  /* fastest this chip does */

static const pico_turbo_config_t pico_turbo_tuned_configs[PICO_TURBO_TUNED_COUNT] = {
        { 235000u, 11u }, /* 235 MHz @ sel 11 */
        { 360000u, 13u }, /* 360 MHz @ sel 13 */
        { 390000u, 14u }, /* 390 MHz @ sel 14 */
        { 420000u, 15u }, /* 420 MHz @ sel 15 */
};
---8<--- END pico_turbo_config.h ---8<---
```

Cut it out, save it next to your sources, and use it:

```c
#include "pico_turbo_config.h"

int main(void) {
        /* The table is the search result, so there is nothing to search for: */
        pico_turbo_use_table(pico_turbo_tuned_configs, PICO_TURBO_TUNED_COUNT);
        pico_turbo_select(PICO_TURBO_TUNED_MAX);   /* or SAFE, or any index */
        ...
}
```

The table describes *the chip it was measured on*, which is the point: two boards
of the same model do not have the same ceiling, and a table measured on one is a
guess on the other.  Every tier is the fastest frequency that chip was found to
run correctly at that core voltage, and it only contains tiers that survived the
example's own acceptance run -- one that fails there is left out and reported.

Notes:

* The build using the table must allow the frequencies in it:
  `PICO_TURBO_MAX_CLK_KHZ` has to reach the top tier, and a configuration above
  either that or the platform's regulator range is refused rather than applied.
* A tier that hangs or crashes the chip during verification is recorded in the
  watchdog scratch registers, so the reset the watchdog causes is attributed to
  that tier on the next boot instead of taking the whole run down with it.
* `-DTUNE_STRESS_MS`, `-DTUNE_STEP_KHZ`, `-DTUNE_MAX_KHZ` and `-DTUNE_LEAVE_AT_MAX`
  change how hard the example looks and what it leaves running (the safmost tier
  by default: a chip left at its highest tier is the harder one to reflash).

| Function | Description |
|---|---|
| `pico_turbo_use_table(configs, count)` | Hand the library a table of configurations |
| `pico_turbo_tier_count()` | How many tiers that table has |
| `pico_turbo_tier(i)` | One tier, or a zeroed configuration if there is no such tier |
| `pico_turbo_select(i)` | Apply a tier; returns whether the frequency was reached exactly |
| `pico_turbo_tiers_from_trace(out, max)` | Reduce the last search's trace to one tier per voltage |

## Configuration

| Variable | Default | Description |
|---|---|---|
| `PICO_TURBO_SYS_CLK_KHZ` | *(empty)* | Target CPU clock in kHz (e.g. `400000` for 400 MHz). Empty = no overclock. |
| `PICO_TURBO_FLASH_CLK_DIV` | *(auto)* | Flash SPI clock divider. Leave empty for automatic; set manually for finicky flash chips. |
| `PICO_TURBO_FLASH_MAX_KHZ` | `133000` | Ceiling the automatic divider keeps the XIP clock under. Lower it for a slow flash chip. |
| `PICO_TURBO_VREG_VOLTAGE` | *(auto)* | Core voltage, e.g. `VREG_VOLTAGE_1_30`. Leave empty to auto-select from frequency. |

Valid values for RP2040: `VREG_VOLTAGE_DEFAULT`, `VREG_VOLTAGE_1_20`, `VREG_VOLTAGE_1_25`, `VREG_VOLTAGE_1_30`.
For RP2350 the extended range (`VREG_VOLTAGE_1_35` … `VREG_VOLTAGE_3_30`) is also available.

### Command-line

```bash
cmake -DPICO_TURBO_SYS_CLK_KHZ=400000 ..
cmake -DPICO_TURBO_SYS_CLK_KHZ=512000 -DPICO_TURBO_FLASH_CLK_DIV=9 ..
cmake -DPICO_TURBO_SYS_CLK_KHZ=400000 -DPICO_TURBO_VREG_VOLTAGE=VREG_VOLTAGE_1_25 ..
```

### In CMakeLists.txt

```cmake
set(PICO_TURBO_SYS_CLK_KHZ 400000)
set(PICO_TURBO_FLASH_CLK_DIV 4)                # optional override
set(PICO_TURBO_VREG_VOLTAGE VREG_VOLTAGE_1_25) # optional override
add_subdirectory(libs/pico-turbo)
target_link_libraries(my_app pico_turbo)
```

## What It Does

`pico_turbo_init()` performs three steps, in order:

1. **Voltage** — raises core voltage to match the target frequency (RP2040: 1.10–1.30 V; RP2350: 1.10–1.60 V with voltage limit disabled).
2. **Stabilise** — waits for the voltage regulator to settle (2× the SDK's own minimum, `SYS_CLK_VREG_VOLTAGE_AUTO_ADJUST_DELAY_US`).  The busy-wait converts the delay with `clock_get_hz(clk_sys)`, i.e. the clock that is running *before* the change.
3. **Clock** — reconfigures the system PLL and `clk_sys` to the target speed, and points `clk_peri` at it.
4. **Verify** — reads the achieved `clk_sys` back and records whether it matched the request exactly (see `pico_turbo_get_status()`), because the voltage was picked for the frequency that was asked for.

## Platform Support

| Platform | Max clock | Boot2 flash DIV | Notes |
|---|---|---|---|
| RP2040 | 420 MHz | 2 (even only) | `VREG_VOLTAGE_1_30` maximum |
| RP2350 | 520 MHz | 4 | Uses `vreg_disable_voltage_limit()` for > 1.30 V |

## Flash Divider

The XIP flash clock is `sys_clk / PICO_FLASH_SPI_CLKDIV`. The library auto-computes the smallest valid divider that keeps flash ≤ 133 MHz:

- **RP2040**: divider must be **even** (2, 4, 6, …)
- **RP2350**: any integer ≥ 2

At extreme frequencies the simple formula may not be conservative enough — if you see instability, override manually:

```bash
cmake -DPICO_TURBO_FLASH_CLK_DIV=9 ..
```

With `PICO_TURBO_AUTOTUNE` the divider is computed for `PICO_TURBO_MAX_CLK_KHZ`,
not for the starting frequency: it has to be safe for every candidate the search may
reach, or the flash would be the thing that fails first -- and a divider that is too
fast is invisible until the image reads back wrong, which is what the self-check
hashes a region of the image to catch.

The divider has to be right from the first XIP access, before `main()` runs, so it belongs to the boot stage 2: the library patches `bs2_default` whenever the computed divider differs from the board's default — a *lower* divider matters too, otherwise the flash stays slower than it needs to be.  Call `pico_sdk_init()` before `add_subdirectory(pico-turbo)`; if `bs2_default` does not exist yet the patch is skipped and CMake warns.

## Benchmark Example

```bash
cd examples/benchmark

# Stock (125 MHz)
mkdir build && cd build && cmake .. && make -j4

# Overclocked (400 MHz)
mkdir build_oc && cd build_oc
cmake -DPICO_TURBO_SYS_CLK_KHZ=400000 .. && make -j4

# Overclocked + run from SRAM (bypass flash XIP bottleneck)
cmake -DPICO_TURBO_SYS_CLK_KHZ=400000 -DCOPY_TO_RAM=ON .. && make -j4
```

Flash both `.uf2` files and compare the serial output (115200 baud). The benchmark runs a prime sieve and a Mandelbrot escape-time computation, reporting microseconds elapsed — a direct measure of your overclock gain.

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

Call `pico_turbo_init()` once at the very start of `main()`, before `stdio_init_all()` or any peripheral setup. When overclocking is disabled the function is empty — safe to leave the call in place for both stock and turbo builds.  A second call does nothing.

`pico_turbo_state()` then tells you what actually happened:

| Field | Meaning |
|---|---|
| `enabled` / `tuned` | built with a target; the configuration came from a search |
| `reached` | `clk_sys` hit the request exactly |
| `requested_khz` / `sys_clk_khz` | what was asked for, what it is now |
| `peri_clk_khz` | `clk_peri` — anything derived from it has to follow |
| `usb_clk_khz` / `usb_ok` | has to stay 48000; it comes from the separate USB PLL |
| `flash_clk_khz` | `clk_sys / PICO_FLASH_SPI_CLKDIV`, i.e. what boot2 was built for |
| `vreg_sel` | the regulator setting in the SDK's `VREG_VOLTAGE_*` encoding |
| `hangs` | candidates the watchdog had to reset during the search |

`pico_turbo_apply()` is the mechanism on its own: voltage first, a settle delay, then
the clock, then `clk_peri`.  A frequency the PLL cannot hit exactly is left where it
was rather than rounded, and the return value says whether it was reached — walk your
own ladder with `check_sys_clock_khz()` if you are doing that.

`pico_turbo_self_test(ms)` runs a fixed, self-checking integer workload plus a hash of
a region of the image read back through XIP, for `ms` milliseconds, and returns its
hash.  The hash is a function of the work alone, so comparing it with one taken on a
chip you trust is how you find out whether a frequency, a voltage or a flash timing is
really safe — a chip that runs but computes wrong answers is the usual failure mode,
and a crash test does not catch it.

## After the call: what else has to follow

Nothing follows the clock for you.  Anything whose timing is fixed at *compile* time from the peripheral or system clock is now wrong:

- a PIO program's clock divider computed from a hard-coded `clk_sys` (divide by the *runtime* `clock_get_hz(clk_sys)` instead),
- a hard-coded SPI baud rate (use `spi_init()`, which reads `clk_peri` at runtime), or a PWM wrap tuned for the old frequency,
- any loop calibrated in cycles.

USB is the exception: `clk_usb` is fed by the separate 48 MHz PLL, so USB keeps working at any `clk_sys` — and, for the same reason, overclocking does not make USB any faster.

## Tests

The half of the library that is not silicon-dependent -- the order the regulator and
the clock are touched in, the refusal of configurations that are not configurations,
reducing a search trace to a tier table, the bounds a search keeps to -- is tested on
the host, with no board attached:

```bash
make -C test/host        # build and run
```

The library is compiled with `-Wall -Wextra -Werror` against stubs for the few
pico-sdk calls it makes, which record every hardware effect it asks for as a log the
tests assert on.  What remains -- whether *this* RP2040 does 420 MHz -- can only be
answered on a chip.

## Troubleshooting

If the Pico fails to boot, hangs, or behaves erratically after overclocking:

0. **Check what it actually got.** `pico_turbo_state()` reports the requested and the achieved `clk_sys`, the peripheral/USB/flash clocks and the regulator setting — that is usually enough to see whether the frequency, the voltage or the flash divider is the problem.
1. **Increase the flash divider.** The flash chip may not keep up at the auto-computed speed. Try a higher `PICO_TURBO_FLASH_CLK_DIV` to lower the flash clock:
   ```bash
   cmake -DPICO_TURBO_FLASH_CLK_DIV=6 ..
   cmake -DPICO_TURBO_FLASH_CLK_DIV=9 ..   # if still unstable
   ```
2. **Run from SRAM.** When the flash simply cannot match the system clock, copy the entire firmware to SRAM at boot. This eliminates the XIP bottleneck entirely (at the cost of reduced available RAM):
   ```bash
   cmake -DCOPY_TO_RAM=ON ..
   ```
   Or in CMakeLists.txt:
   ```cmake
   pico_set_binary_type(your_app copy_to_ram)
   ```

## ⚠️ Warning

Overclocking increases power consumption and heat, and **may permanently damage your device**. Use at your own risk. The library is provided as-is with no warranty.

## License

MIT — see [LICENSE](LICENSE).
