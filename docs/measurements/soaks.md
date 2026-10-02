# Soak records

> Long repeated runs are what turn "one run passed" into "this configuration is usable";
> every soak below rebooted between runs and every reported run validated.

## Official Pico 2, 520 MHz / 1.60 V, both cores, 50 runs

CoreMark with one context per core, 34666 iterations each, 520 MHz at 1.60 V, divider 8
(65 MHz flash clock), fifty consecutive runs, each rebooting through the bootrom so no
run inherits a warm chip.

| Quantity | Result |
| --- | --- |
| Runs that produced a score | 50 of 50 |
| Runs that validated | 50 of 50 |
| `Errors detected` | 0 |
| Iterations/sec | mean 2622.081163, min 2622.078177, max 2622.083036 |
| Spread | 0.0002 % — 30 distinct scores in 50 runs |
| Clock state lines reading 520000 kHz asked, configured and measured | 50 of 50 |

The Luckfox board's equivalent soak (36 runs, 2612.7 iterations/sec) is 0.36 % slower with
a spread of 0.39 % rather than 0.0002 % — same chip and firmware, so what differs is the
board's power supply, and it shows up as jitter well before it shows up as failure. A
search choosing a tier under that supply is choosing with less margin than the score
alone suggests.

## Official Pico W, 440 MHz / 1.30 V, both cores, 30 runs

30 consecutive dual-core runs at 440 MHz (divider 4, so 110 MHz of flash clock — above
the 105 MHz ceiling the board file carried when this was measured; it carries 110 MHz
now), every run validated, no errors, a 0.0009 % spread and a mean of 1484.793434
iterations/sec. The score was identical at the file's ceiling and above it, which says
the load lives in the XIP cache. 440 MHz is the highest
the RP2040's regulator is documented for, so this board sits at the corner of the policy's
box *and* holds there.

The AirMech RP2040's 440 MHz locked up, so this is a statement about the official board,
not the chip.

## Official Pico W, 420 MHz / 1.30 V, both cores, 27 runs

Soaked at 420 MHz with both cores, 20160 iterations each and a bootrom reboot between
runs: 27 consecutive complete runs, every one validated, no errors, mean 1417.301508
iterations/sec with a 0.0059 % spread and every run's 28.449 s within 1.7 ms of the next.
That tier is the corner of the box the library defines for RP2040 — the platform's clock
ceiling and the regulator's documented maximum reached at once — and it holds under both
cores for as long as this board was asked to hold it.

## WeAct RP2350A, 520 MHz / 1.60 V, both cores, ten-run attempts

One ten-run soak passed (10 of 10, spread 0.0041 %) under GCC 13.2.1. Under GCC 16.2.0
the same soak has not once run to the end: four attempts came back with **6, 6, 7 and 5
of 10 runs**. The debugger read two of them: the 7-of-10 one was spinning in
`core_stop_parallel` (core 0 waiting on core 1 with no timeout, CFSR zero); the 5-of-10
one had PC `0xeffffffe` with CFSR `0x8200`, a precise bus fault. Both are the heaviest
load this board can be given, and the official Pico 2 fails joining core 1 at 546 MHz
while the 2 A-supplied Luckfox board does not fail at 520. The full interpretation is in
[clock-ceilings.md](clock-ceilings.md).

## What a soak is not

The library's own search verifies each candidate for `PICO_TURBO_STRESS_MS` (default
30 ms) and the tune example re-verifies a tier for `TUNE_VERIFY_MS` (default 200 ms).
That is a screen for a chip computing wrong answers **now**; it is not a stability test.
On a measured Pico 2 the screen accepted 570 MHz at 1.60 V, where the same CoreMark build
could not bring up USB. The soak records above are the acceptance, not the screen.

## Verification of the pico_w default

A build asking only for `-DPICO_BOARD=pico_w` announced the default, resolved to 440 MHz
with a 110 MHz flash clock, came up at 440000 kHz measured with vreg sel 15, and
validated 832.21 iterations/sec — the number the point was measured at with every
parameter passed explicitly. This is checked against the board, not against the file.

## Related

- [clock-ceilings.md](clock-ceilings.md) — ceilings and their interpretation
- [flash-ceiling.md](flash-ceiling.md) — the divider points in these runs
- [toolchain-validity.md](toolchain-validity.md) — why the compiler is named per row
