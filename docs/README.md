# pico_turbo knowledge base

This directory explains *why* the clock/voltage/flash-divider library is written the
way it is, and records what was measured on which board. The user-facing entry point
is the repository [README](../README.md) (Chinese mirror: [README_zh.md](../README_zh.md));
maintenance rules are in [AGENTS.md](../AGENTS.md).

Language: the knowledge base is English, matching the primary README. Only the README
has a Chinese mirror, and the two READMEs must stay equivalent.

## Documents

| Document | Question it answers |
| --- | --- |
| [configuration.md](configuration.md) | Every CMake switch, board profile, auto-tune knob and resolved variable the build exposes |
| [design.md](design.md) | How `pico_turbo_init()`/`apply()` work: the order voltage and clock are touched, the state it reports, what the caller must fix afterwards |
| [flash-divider.md](flash-divider.md) | How the flash divider is derived, why it is per board, the RP2040 even rule and the boot2 patch |
| [autotune.md](autotune.md) | The runtime search, the watchdog/scratch protocol, and the per-chip tier header `examples/tune` emits |
| [boards.md](boards.md) | The board files, their profiles and ceilings, and the coremark → `boards/<board>.cmake` contract |
| [troubleshooting.md](troubleshooting.md) | A build that will not run, runs wrong, or locks up |
| [testing.md](testing.md) | The offline host suite, test oracles, exit codes, and the `tools/turbocfg.py` CLI |
| [measurements.md](measurements.md) | Index of the measurement records below |

## Measurement records

| Document | Topic |
| --- | --- |
| [measurements/toolchain-validity.md](measurements/toolchain-validity.md) | Why a CoreMark number is compiler-specific, and that compile options are not the lever |
| [measurements/clock-ceilings.md](measurements/clock-ceilings.md) | Each board's CPU clock ceiling and what the ceiling belongs to (chip vs board) |
| [measurements/voltage-tiers.md](measurements/voltage-tiers.md) | The voltage each clock needs, measured across boards of both platforms |
| [measurements/flash-ceiling.md](measurements/flash-ceiling.md) | Flash divider ladders and the per-board flash ceiling |
| [measurements/soaks.md](measurements/soaks.md) | Long repeated-run records: dual-core soaks and their spreads |
| [measurements/method-notes.md](measurements/method-notes.md) | Measurement traps that have produced wrong conclusions in this project |

## Maintenance conventions

- Every number states the board and the build it came from; ratios are kept with their
  compiler. Unverified claims are marked "not verified".
- Board files are the landing point for stable configurations measured in the `coremark`
  repository; update [boards.md](boards.md) when a board file changes.
- No host absolute paths, internal IPs, credentials, proxy addresses or board serial
  numbers. Use `<pico-sdk>`, `<this repo>`, `<board-id>` placeholders.
- Merge and rewrite an existing document rather than appending dated updates.
- Keep documents under ~150 lines; split by question when one grows past ~300.
