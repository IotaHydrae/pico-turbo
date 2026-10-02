# Auto-tune and per-chip tier tables

> The search answers "how far does **this** die clock" once, on the chip; `examples/tune`
> writes that answer down as a C header of tiers. What it accepts is a short screen, not
> a stability test — treat the top tier as a candidate.

## TL;DR

- `PICO_TURBO_AUTOTUNE=1` makes `pico_turbo_init()` walk `PICO_TURBO_STEP_KHZ` upward
  from `PICO_TURBO_SYS_CLK_KHZ`, raise voltage when a step fails, self-check each
  candidate, and keep the last configuration that passed.
- A candidate that **hangs** is caught by the watchdog; the candidate on trial was
  written to the watchdog scratch registers first, so the next boot records it as hung
  and comes back at that frequency one voltage step up. After
  `PICO_TURBO_MAX_HANGS` (default 2) resets it stops at the starting frequency.
- The best configuration is kept in scratch as well: a warm reset reuses it, a power
  cycle starts over. `pico_turbo_autotune(&policy)` runs a search later with a caller's
  own policy.
- A step the PLL cannot hit exactly is recorded `SKIPPED`, not rounded.
- A 30 ms (default) pass is a screen. On a measured Pico 2 it accepted 570 MHz at
  1.60 V, where the same CoreMark build could not bring up USB; three consecutive
  CoreMark runs at 520 MHz / 1.60 V *did* validate. Long mixed workloads are what
  acceptance means — see [measurements/clock-ceilings.md](measurements/clock-ceilings.md).

## Where the search runs

At the very start of `main()`, before `stdio_init_all()` or any peripheral setup. A
candidate that hangs is reset by the watchdog and the program restarts; the records left
in scratch tell the next boot what happened. The search therefore must not depend on
anything an earlier part of `main()` configured.

The policy fields (`pico_turbo_autotune_t`) all default to the compile-time values when
left zero: `base_khz`, `max_khz`, `step_khz`, `stress_ms`, `min_vreg_sel`, `max_vreg_sel`,
`max_hangs`.

## The scratch protocol

`pico_turbo_tune.c` uses `watchdog_hw->scratch[0..3]` and leaves slot 4 to the SDK's
`watchdog_caused_reboot()`. Magic `'ptun'` (0x7074756e) in slot 0 marks the records;
slot 1 is the candidate in flight, slot 2 the best that passed, slot 3 the reset count.
Slot 6 is spare: it holds where a resume decided to start, for a debugger to read back.
A fresh power-up does not have the magic, so none of it is trusted.

## From trace to tiers

`pico_turbo_tiers_from_trace(out, max)` reduces the last search's trace to the highest
stable frequency at each voltage, oldest first. Entry 0 is the starting configuration, so
"tier 0" means "where I began"; the last entry is what the top voltage bought. It is the
same reduction `examples/tune` uses before it re-verifies every tier.

The trace holds `PICO_TURBO_TRACE_STEPS` (64) entries and stops recording beyond that —
`pico_turbo_trace(&steps)` returns the count. A tier table holds `PICO_TURBO_TIERS_MAX`
(8) entries by default, which is the size the tune example allocates. A climb fine enough
to cross many PLL points can truncate, so the highest accepted tier must be maintained
incrementally rather than assumed to be the last line of a long trace (open item, see
[../AGENTS.md](../AGENTS.md)).

## The tune example

`examples/tune` searches with a harder policy than a boot would (`TUNE_STRESS_MS` 100,
`TUNE_VERIFY_MS` 200), re-verifies each tier, and prints a header between markers:

```text
---8<--- BEGIN pico_turbo_config.h ---8<---
/* ... board id : <board-id> ... measured : <date> ... tiers : 4 */
#define PICO_TURBO_TUNED_BOARD_ID "<board-id>"
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

The table describes **the chip it was measured on**: two boards of the same model do not
have the same ceiling, and a table measured on one is a guess on the other. It contains
only tiers that survived the example's own acceptance run; a tier that fails is left out
and reported. `-DTUNE_LEAVE_AT_MAX=0` (default) leaves the chip at the safest tier,
because a chip left at its highest tier is the harder one to reflash.

Notes:

- The build using the table must allow its frequencies: `PICO_TURBO_MAX_CLK_KHZ` has to
  reach the top tier, or the configuration is refused rather than applied.
- A tier that hangs during verification is recorded in scratch, so the watchdog reset is
  attributed to that tier on the next boot instead of taking the run down.
- The tune example's acceptance is still a screen. `examples/tune` is for measuring; a
  long mixed workload (the CoreMark port in the `coremark` repository) is for shipping.

## API

| Function | Description |
| --- | --- |
| `pico_turbo_autotune(policy)` | Run a search now; `NULL` uses the compile-time defaults. |
| `pico_turbo_use_table(configs, count)` | Hand the library a table kept by pointer. |
| `pico_turbo_tier_count()` | How many tiers that table has. |
| `pico_turbo_tier(i)` | One tier, or a zeroed configuration if there is no such tier. |
| `pico_turbo_select(i)` | Apply a tier; returns whether the frequency was reached exactly. |
| `pico_turbo_tiers_from_trace(out, max)` | Reduce the last search's trace to one tier per voltage. |
| `pico_turbo_trace(&steps)` | The last search's trace, oldest first. |

`pico_turbo_select()` refuses a tier that is not a valid configuration (zero frequency,
above the build maximum, or a regulator encoding above the policy ceiling) without
publishing state for it.

## Related

- [configuration.md](configuration.md) — the knobs above
- [design.md](design.md) — `apply()`'s guarantees
- [measurements/soaks.md](measurements/soaks.md) — what a long acceptance run looks like
