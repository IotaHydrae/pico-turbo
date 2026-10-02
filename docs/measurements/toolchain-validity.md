# What a CoreMark number depends on

> A CoreMark score here is compiler-specific and optimization-level-specific; ratios and
> per-MHz slopes survive a compiler change, absolute constants do not.

## TL;DR

- The same source built by GCC 16.2.0 (every result in this knowledge base) scores
  **5.30 % higher** than Debian's GCC 13.2.1 at the same clock and voltage — a plain
  multiplier, the same at 150 MHz as at 520 and the same with two cores as with one.
- On one board, six GCC releases: 13.2.0 is fastest at 1543.05 iterations/sec and
  2.9674 per MHz; the two 16.x releases are identical to each other and 5.30 % behind.
- `-O2` costs 0.41 % and `-Os` 17.9 %, and each costs **exactly the same percentage at
  150 MHz as at 520 MHz** (to 0.0001 of a point). So the score is instructions executed,
  not instruction fetch; the 48 % smaller `-Os` image does not do better at the higher
  clock.
- `-fno-unroll-loops` and `-fno-ipa-cp-clone` produce byte-identical code to `-O3` here;
  `-flto` does not link against this SDK's linker script (`dangerous relocation`).
- The SDK never set an optimization level of its own: `-O3` is CMake's `Release` default,
  and the SDK's only compile flags are `-mcpu/-mthumb/-march/-mfloat-abi/-mcmse`.
- The dual-core factor is compiler-dependent too: between **1.7745 and 1.8319**, so a
  dual-core number is only comparable inside one compiler.

## Consequences for reading the records

- Ratios survive a compiler change when both sides of a comparison came from one
  compiler. The 1.490x an M33 does over an M0+ was measured across two boards under one
  compiler and is such a ratio.
- Absolute per-MHz constants are that compiler's, not the chip's. Every result in
  `docs/` was built by `arm-none-eabi-gcc` GCC 16.2.0 unless a row says otherwise
  (the WeAct table names GCC 13.2.1 where it differs).
- One RP2040 point under GCC 13.2.0 is the measurement that would say whether the M0+
  moves by the same 5.30 %; it has not been run.

## Sources

- CoreMark repository `RANKINGS.md` section 8 (toolchain multiplier) and
  `TOOLCHAINS.md` (the seven-release ladder with download links).
