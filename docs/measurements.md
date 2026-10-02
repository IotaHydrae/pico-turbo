# Measurement records

> The raw numbers behind the library's defaults and the claims in the READMEs. One board
> each unless a table says otherwise; a record of a measurement, not a promise about any
> other board.

CoreMark results, which is where the stability claims come from, are in the CoreMark
port's own notes and `RANKINGS.md`. The library-side interpretation is here.

| Record | Topic |
| --- | --- |
| [toolchain-validity.md](measurements/toolchain-validity.md) | Why a score is compiler-specific, and that optimization level is not the lever |
| [clock-ceilings.md](measurements/clock-ceilings.md) | Per-board CPU clock ceilings, chip vs board, derived cross-platform numbers |
| [voltage-tiers.md](measurements/voltage-tiers.md) | Voltage needed per clock, measured on both platforms |
| [flash-ceiling.md](measurements/flash-ceiling.md) | Divider ladders and the per-board flash ceiling |
| [soaks.md](measurements/soaks.md) | Long repeated-run records |
| [method-notes.md](measurements/method-notes.md) | Traps in collecting and comparing measurements |

How to read a number here:

- Every result in these records was built by `arm-none-eabi-gcc` GCC 16.2.0 unless a row
  names another compiler. Ratios and per-MHz slopes survive a compiler change; absolute
  constants do not — see [toolchain-validity.md](measurements/toolchain-validity.md).
- Board names are as used in the `coremark` repository; the board files that carry the
  conclusions are listed in [boards.md](boards.md).
