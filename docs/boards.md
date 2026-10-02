# Board files

> `boards/<board>.cmake` is where measurements from the `coremark` test bench land: the
> ceilings and profiles an application actually links. A board with no file gets the
> CMake platform fallback.

## TL;DR

- A board file sets `_PLATFORM_MAX_KHZ`, optionally `_FLASH_MAX_KHZ`,
  `_BOOT2_DEFAULT_DIV`, `_FLASH_REQUIRES_EVEN`, and named profiles.
- `pico_w` is the only file with a **default** profile; asking for nothing gets its
  measured extreme (440 MHz, 1.30 V, DIV 4). `-DPICO_TURBO_PROFILE=none` opts out.
- All ceilings are overridable from the command line (`-D_PLATFORM_MAX_KHZ`,
  `-D_FLASH_MAX_KHZ`), which is how a board's real limit is probed.
- Do not edit the measured numbers here without a new measurement; a wrong wall is worse
  than a conservative one.

## What is in each file

| File | `_PLATFORM_MAX_KHZ` | `_FLASH_MAX_KHZ` | even DIV | boot2 default | Profiles (`PICO_TURBO_PROFILE=`) | Default |
| --- | --- | --- | --- | --- | --- | --- |
| `pico.cmake` | 420000 | *unset* → 133000 | ON | 2 | `safe` 240 stock; `turbo` 360 @1.20 V; `extreme` 400 @1.30 V | none |
| `pico_w.cmake` | 440000 | 110000 | ON | 2 | `safe` 240 stock; `fast` 300 @1.20 V; `turbo` 360 @1.20 V; `extreme` 440 @1.30 V (all DIV 4) | **extreme** |
| `pico2.cmake` | 564000 | 57000 | OFF | 2 | `safe` 300 @1.20 V; `fast` 400 @1.30 V; `turbo` 520 @1.60 V; `extreme` 564 @1.60 V | none |
| `weact_rp2350a.cmake` | 520000 | 52000 | OFF | 2 | `safe` 300; `fast` 400; `turbo` 500; `extreme` 520 (voltage from the library table) | none |

The evidence behind each number, per board, is in the measurement records:
[clock ceilings](measurements/clock-ceilings.md),
[flash ceilings](measurements/flash-ceiling.md), [soaks](measurements/soaks.md).

`pico2` and `weact_rp2350a` have `_FLASH_REQUIRES_EVEN OFF` because RP2350's `w25q080`
boot stage 2 checks only a maximum; the RP2040 files are ON because their boot stage 2
refuses an odd divider outright. Rationale and alternatives:
[flash-divider.md](flash-divider.md). Neither RP2350 file has a default profile: on
`pico2` the name covers the official board and clones with different flash parts and
supplies, and on `weact_rp2350a` naming a board should not silently overclock it.

`weact_rp2350a` pins no voltage and no divider on purpose: what a build gets is what was
measured on that board, and the divider arithmetic stays in the library where the
parity rule lives. A profile that pinned DIV 9 on a Pico 2 could not be configured at
all.

## The coremark contract

Finding ceilings and stability is the job of the `coremark` repository (the test bench).
Its `tools/probe.py` runs the clock ladder, the flash-divider ladder, a dual-core point
and a soak, then writes a report and a candidate `boards/<board>.cmake`. That file — not
the probe log — is what an application links, so a stable configuration measured there
**must** have a landing point here or applications cannot reach it. `boards/pico_w.cmake`
was produced by exactly that flow.

The probe's generated file is a *proposal*: it must be read and checked against the
current file before it replaces anything, because the same board name can cover a
different flash chip.

## Adding or updating a board file

1. Measure on the bench (`coremark/tools/probe.py --board <name>`), with the point
   verified end to end and the flash read back before any loop.
2. Compare the proposal with the existing file; keep the measured numbers and the
   comment that says where they came from.
3. Update the table above and the topic record under `docs/measurements/`.
4. `make -C test/host` and `tools/turbocfg.py board show boards/<name>.cmake` to confirm the
   file parses and the arithmetic matches what the library derives.

## Related

- [configuration.md](configuration.md) — how a profile is selected
- [flash-divider.md](flash-divider.md) — the divider rules behind the files
- [testing.md](testing.md) — `turbocfg board show` / `board propose`
