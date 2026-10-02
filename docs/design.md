# How the library changes the chip

> `pico_turbo_apply()` moves the regulator first when going up and the clock first when
> going down — one direction only locks the chip up and needs a power cycle.

## TL;DR

- Order on the way **up**: raise voltage → settle → raise clock → point `clk_peri` at it.
  On the way **down**: lower clock → lower voltage. Reversing either edge was measured
  as a lockup, not a wrong frequency.
- `pico_turbo_init()` is the build-time policy: apply the fixed target, or run the search
  when `PICO_TURBO_AUTOTUNE` is on. It is a no-op in a build without a target.
- A frequency the PLL cannot produce exactly is **left where it was** rather than rounded;
  the return value and `pico_turbo_state().reached` say whether the request was hit.
- Nothing follows the clock for you: anything timed from `clk_sys`/`clk_peri` at compile
  time must be recomputed. USB is the exception (separate 48 MHz PLL).

## The order (the reason this library exists)

Going up, the supply has to be there before the core runs faster than the old voltage
supports. Going down, the core has to come off the old frequency before the supply is
cut, or it briefly runs the high frequency at the low voltage.

The code decides with `lower_clock_first = config->khz < now_khz` and then calls
`set_clock()` before `vreg_set_voltage()` on the way down, and after the settle delay on
the way up (`pico_turbo.c:pico_turbo_apply()`). A search only climbs, so it only ever
exercises one edge; the first thing an application does after a search — dropping to a
lower tier — exercises the other one. Emitting the two edges as one order fails one of
them. The measured consequence of the wrong order at a low voltage is a locked-up chip
that only a power cycle recovers.

Above the platform maximum, `vreg_disable_voltage_limit()` is called first (RP2350;
no-op on RP2040).

## The settle delay

`PICO_TURBO_SETTLE_US` is twice the SDK's own minimum
(`SYS_CLK_VREG_VOLTAGE_AUTO_ADJUST_DELAY_US * 2`). The busy-wait converts microseconds
with `clock_get_hz(clk_sys)` — the clock running **before** the change, which is what
`busy_wait_at_least_cycles()` counts. Converting with XOSC cycles instead (12 MHz against
a 125 MHz core) would make the wait roughly ten times shorter than claimed.

## Voltage table (starting points)

`pico_turbo_voltage_for_khz()` maps frequency to the lowest voltage each range is known
to work at. These are the search's starting points and a fixed build's values, not the
truth about a given die.

| RP2040 | Voltage |
| --- | --- |
| ≤ 266 MHz | `VREG_VOLTAGE_DEFAULT` (1.10 V) |
| ≤ 360 MHz | 1.20 V |
| ≤ 396 MHz | 1.25 V |
| > 396 MHz | 1.30 V (platform maximum) |

| RP2350 | Voltage |
| --- | --- |
| ≤ 150 MHz | `VREG_VOLTAGE_DEFAULT` (1.10 V) |
| ≤ 300 MHz | 1.20 V |
| ≤ 384 MHz | 1.30 V |
| ≤ 440 MHz | 1.40 V |
| ≤ 500 MHz | 1.50 V |
| > 500 MHz | 1.60 V (policy maximum) |

A build that pins `PICO_TURBO_VREG_VOLTAGE` does not carry the table at all. The
cross-board evidence that these tiers are the **chip's** and not the board's is in
[measurements/voltage-tiers.md](measurements/voltage-tiers.md).

## What the state reports

`pico_turbo_state()` returns the clocks as read back plus the configuration in effect:

| Field | Meaning |
| --- | --- |
| `enabled` / `tuned` | Built with a target; the configuration came from a search. |
| `reached` | `clk_sys` hit the request exactly. |
| `requested_khz` / `sys_clk_khz` | Asked for / now. |
| `peri_clk_khz` | `clk_peri`; anything derived from it has to follow. |
| `usb_clk_khz` / `usb_ok` | Must stay 48000; it comes from the separate USB PLL. |
| `flash_clk_khz` | `clk_sys / PICO_TURBO_FLASH_DIV`, i.e. what boot2 was built for. |
| `vreg_sel` | Regulator setting in the SDK's `VREG_VOLTAGE_*` encoding (**not** mV). |
| `hangs` | Candidates the watchdog had to reset during a search. |

`pico_turbo_apply()` refuses — without touching the hardware — a zero frequency, a
frequency above `PICO_TURBO_MAX_CLK_KHZ`, or a `vreg_sel` above
`PICO_TURBO_MAX_VREG_SEL`. The regulator controls are not the place to discover that a
table held nonsense: `VREG_VOLTAGE_*` values are register encodings, so an out-of-range
one lands in the low bits of the voltage-select field and asks for a supply the core
cannot run at.

## After the call: what has to follow

Anything whose timing is fixed at **compile** time from `clk_sys` or `clk_peri` is now
wrong:

- a PIO program's clock divider computed from a hard-coded `clk_sys` (divide by the
  runtime `clock_get_hz(clk_sys)` instead),
- a hard-coded SPI baud rate (use `spi_init()`, which reads `clk_peri` at runtime), or a
  PWM wrap tuned for the old frequency,
- any loop calibrated in cycles.

USB is the exception: `clk_usb` is fed by the separate 48 MHz PLL, so USB keeps working
at any `clk_sys` — and overclocking does not make USB any faster for the same reason.

## Platform fallbacks

When no board file matches, `CMakeLists.txt` uses a platform fallback: RP2040 ceiling
420 MHz, boot2 default DIV 2, even divider required; RP2350 ceiling 520 MHz, boot2
default DIV 4, odd divider allowed. These are CMake fallbacks, **not** the ceilings of a
particular board — `boards/pico2.cmake` raises the ceiling to 564 MHz and keeps
`_BOOT2_DEFAULT_DIV 2`. The per-board numbers are in [boards.md](boards.md).

## Related

- [flash-divider.md](flash-divider.md) — the divider itself
- [autotune.md](autotune.md) — the search that uses `apply()`
- [configuration.md](configuration.md) — the switches that select all of this
