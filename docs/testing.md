# Testing and the offline tool

> `make -C test/host` runs the whole offline suite — the C policy tests and the
> `turbocfg` tests — with no board and no SDK; every PASS/FAIL states its oracle, and no
> expected value came from a previous run.

## TL;DR

- `make -C test/host` builds `test/host/test_turbo` (library against stubs) and runs
  `test/host/test_turbocfg.py` (the `tools/turbocfg.py` CLI).
- The C suite covers the half of the library that is policy, not silicon: the order the
  regulator and clock are touched in, refusal of invalid configurations, trace→tier
  reduction, and search bounds.
- The Python suite adds a **docs↔source drift guard**: `config-table` must equal the
  voltage table compiled into `pico_turbo.c`.
- Exit codes are 0 (PASS) and 1 (FAIL). The library has no usage or environment surface
  in this suite, so codes 2–5 are not invented; `turbocfg` itself uses the full set.
- Whether *this* chip runs at 420 MHz cannot be answered here; see
  [measurements.md](measurements.md).

## Running

```bash
make -C test/host            # everything
make -C test/host check-c    # C suite only
make -C test/host check-tools
make -C test/host clean
```

`PYTHON` selects the interpreter (`PYTHON=python3` by default). The suite is offline and
deterministic: no network, no device, no writes outside the build directory.

The C suite compiles `pico_turbo.c`, `pico_turbo_tune.c` and `pico_turbo_selftest.c`
with `-Wall -Wextra -Werror` against stubs in `test/host/stub/`. Every pico-sdk call the
library can make is recorded as text rather than performed, so a test asserts the
**sequence** (for example that `set_sys_clock_khz` appears before `vreg_set_voltage` when
lowering) and that nothing was touched when a configuration is refused.

## Test oracles

Each test declares `ORACLE` (type), `SOURCE` and `EXPECTED`, printed at run time.

| Test | Oracle | Source | Expected |
| --- | --- | --- | --- |
| `test_lowering_takes_the_clock_down_first` | INVARIANT | [design.md](design.md) ordering; reversed = measured lockup | step down: clock before voltage; step up: the reverse |
| `test_a_hang_is_retried_with_more_voltage` | SPEC | `include/pico_turbo.h` autotune contract; `pico_turbo_tune.c` inflight scratch | hung candidate recorded, retried at the same frequency with ≥1 more voltage step |
| `test_a_wall_at_the_voltage_ceiling_stops_the_climb` | SPEC | `pico_turbo_tune.c` (`hung_sel >= max_vreg_sel`) | the hang is the only trace entry; that frequency is not retried |
| `test_the_hang_budget_is_respected` | SPEC | `pico_turbo_autotune_t.max_hangs` contract | no retry once the budget is spent |
| `test_apply_refuses_what_is_not_a_configuration` | INVARIANT | `pico_turbo.c` `config_valid()` | zero/too-high frequency and out-of-range selector refused, no write |
| `test_tiers_are_the_best_at_each_voltage` | SPEC | `pico_turbo.h` `pico_turbo_tiers_from_trace()` | one entry per voltage, highest stable frequency, climb order |
| `test_selection_refuses_a_bad_tier` | INVARIANT | `config_valid()` via `pico_turbo_select()` | corrupt tier refused, regulator untouched |
| `test_a_search_stops_at_what_the_pll_can_do` | SPEC | `pico_turbo_tune.c` climb loop; SDK `check_sys_clock_khz()` | result within `[base, PLL max]`, trace present, watchdog disarmed |
| `test_board_show_matches_the_documented_files` | SPEC | [boards.md](boards.md) | each board file's ceilings, parity and profiles |
| `test_build_arithmetic_matches_the_documented_dividers` | SPEC | [flash-divider.md](flash-divider.md), [boards.md](boards.md) | the DIV each profile was measured at |
| `test_voltage_table_matches_the_c_source` | RELATIONSHIP | `pico_turbo.c` `turbo_vreg_table[]` | tool table ≡ C table |
| `test_table_emit_round_trips_through_table_show` | RELATIONSHIP | `examples/tune/tune.c` header format | emit → parse yields the same tiers |
| `test_board_propose_refuses_to_invent_a_measurement` | SPEC | exit-code convention | no accepted point → `INCONCLUSIVE` |
| `test_exit_codes_follow_the_workspace_convention` | REQUIREMENT | [`../AGENTS.md`](../AGENTS.md) | 2 usage, 3 environment, 1 fail, 0 ok |

No test has an expected value taken from a previous run; `test_table_emit_round_trips…`
and `test_voltage_table_matches_the_c_source` are relationships, not golden files.

## Exit codes

| Code | Meaning | Who uses it |
| --- | --- | --- |
| 0 | PASS | both suites |
| 1 | FAIL | both suites |
| 2 | INVALID_USAGE | `turbocfg` only |
| 3 | ENVIRONMENT_ERROR | `turbocfg` only (missing file) |
| 4 | TIMEOUT | `turbocfg --timeout` |
| 5 | INCONCLUSIVE | `turbocfg board-propose` with no accepted point |

The C test binary returned 0/1 before this document existed and still does; `make check`
forwards that code, and no consumer parses it beyond make's "recipe failed". The other
codes would have to mean something in an offline suite, so they are left unused rather
than given invented semantics.

## `tools/turbocfg.py`

A stable CLI for the file-level facts around this library: read a board file, reproduce
the build's divider/voltage arithmetic, read or write a per-chip tier header, and turn
measured points into a board-file proposal.

It exists because nothing offline did this: `coremark/tools/probe.py` *writes* a board
file but needs a probe and a board, and the C test's call-log parsing is about the
library's hardware calls, not CMake. No second parser was added — the tool is the only
one, and the tests call it as a subprocess.

| Command | What it does |
| --- | --- |
| `turbocfg board-show FILE` | The ceilings, parity rule and profiles a board file carries |
| `turbocfg board-propose --board N --platform P --points FILE` | A board-file proposal from accepted measurements; exit 5 if there is none |
| `turbocfg table-show FILE\|-` | Tiers in a generated `pico_turbo_config.h` |
| `turbocfg table-emit --tiers KHZ:SEL,...` | Emit that header from a tier list |
| `turbocfg config-explain --platform P --khz N` | The resolved clock/divider/voltage a build would get |
| `turbocfg config-table --platform P` | The frequency→voltage table |

```bash
tools/turbocfg.py board-show boards/pico_w.cmake
tools/turbocfg.py config-explain --platform rp2040 --khz 440000 \
    --board-file boards/pico_w.cmake --verbose
tools/turbocfg.py config-table --platform rp2350 --json
```

`board-propose` takes a JSON points file: a list, or `{"points": [...], "flash_max_khz":
N}`, where each point is `{"khz": …, "result": "ok", "vreg_voltage": …,
"flash_khz": …}`. Only points whose `result` is `ok`/`stable` are used; the four profiles
are picked across the accepted ladder, and the emitted file carries a "review before use"
header. It is a proposal, never a replacement.

Common options: `--json`, `--quiet`, `--verbose`, `--timeout SECONDS` (exit 4), `--help`,
`--version`.

## What this suite cannot cover

- Whether a given die reaches a frequency, at what voltage, and with how much flash
  clock. The stubs model reachability, not silicon.
- The flash divider's real ceiling, the PLL's real point set, and soak stability.
- Anything involving `platform_max`, `flash_requires_even` and profiles *in CMake*, which
  needs an SDK (`turbocfg config-explain` reproduces the arithmetic instead).

Those live in [measurements/](measurements.md) and are produced on the
`coremark` bench.

## Related

- [boards.md](boards.md) — the files `turbocfg board-show` reads
- [flash-divider.md](flash-divider.md) — the arithmetic `config-explain` reproduces
- [../AGENTS.md](../AGENTS.md) — the repo's build/verify entry points
