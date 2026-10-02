# Clock ceilings

> On RP2350A the ~565–570 MHz edge is the silicon (two vendors stop in the same place),
> while the flash ceiling varies more than 2x between the same two boards; on RP2040 the
> voltage each clock needs is the chip's but the top of the ladder is the board's.

## The boards

| Board | Chip | Flash | Notes |
| --- | --- | --- | --- |
| Luckfox Pico 2 | RP2350A rev 2 (A2), QFN60 | Puya PY25Q32HB, 4 MB QSPI *(vendor description, not measured)* | heatsink fitted; 2 A buck-boost on the 3V3 rail |
| AirMech RP2040 (the PUD panel host) | RP2040 | not probed | the board the RP2040 defaults came from |
| Official Pico 2 | RP2350A rev 2 (A2) | Winbond W25Q32FV/JV, 4 MB QSPI (measured: `id = 0x1640ef`, 4096 KiB) | measured; see below |
| Official Pico W | RP2040 B2 | Winbond W25Q16JV, 2 MB QSPI (measured: `id = 0x1540ef`, 2048 KiB) | measured; see below |
| WeAct Studio RP2350A (V1.0) | RP2350A rev 2 | Winbond W25Q32FV/JV, 4 MB QSPI (measured: `id = 0x1640ef`, 4096 KiB) | same flash part as the Pico 2; its own board file because its ceiling is lower and its dual-core margin thinner |

## RP2350

CoreMark on an official Pico 2, 1.60 V, single core, iterations scaled so each run is
~12 s. Every row reports `Correct operation validated`.

| Clock | Iterations/sec | Against 520 MHz |
| --- | --- | --- |
| 520 MHz | 1465.38 | 1.000x |
| 546 MHz | 1538.64 | 1.050x |
| 552 MHz | 1555.56 | 1.062x |
| 558 MHz | 1572.47 | 1.073x |
| 564 MHz | 1589.37 | 1.085x |
| 570 MHz | does not run | -- |

The 570 MHz row is verified rather than inferred: the flash was read back and matched the
build exactly, and the core was found in `isr_hardfault`. The Luckfox board has the same
boundary (564 passes, 570 fails) — two vendors, different flash parts and different
regulators stopping in the same place is why ~565–570 MHz is taken as the **silicon**
edge, not the board's.

With both cores the same official board stops earlier: 520 MHz ran 50 consecutive
dual-core runs (see [soaks.md](soaks.md)), 546 MHz with two cores passed once
(2748.29 iterations/sec) and hung once (PC in `core_stop_parallel`), and 564 MHz with two
contexts hard-faults before printing anything. One core validates to 564; two cores stop
being reliable at 546. `boards/pico2.cmake` therefore hands applications 520 as `turbo`
and marks 564 `extreme` as single-core only.

### Luckfox Pico 2, from the library's own search

5 MHz steps, 100 ms stress per candidate, a 200 ms soak when a tier is re-verified.
`sel` is the regulator register encoding: 11 = 1.10 V, 13 = 1.20 V, 15 = 1.30 V,
17 = 1.40 V, 18 = 1.50 V, 19 = 1.60 V, 21 = 1.70 V.

| Setting | Result |
| --- | --- |
| 300 MHz @ 1.20 V (sel 13) | accepted, and CoreMark validates its results there |
| 380 MHz @ 1.30 V (sel 15) | accepted |
| 440 MHz @ 1.40 V (sel 17) | accepted |
| 520 MHz @ 1.60 V (sel 19) | accepted, and CoreMark validates there (three runs) |
| 570 MHz @ 1.60 V (sel 19) | accepted by this screen -- and CoreMark cannot start there |
| 600 MHz @ 1.60 V (sel 19) | hangs; the watchdog resets and the search comes back |
| 1.70 V (sel 21) | not usable at all: the clock will not move from the base frequency |

With a 2000 ms soak per candidate and per tier the same search settles at
**540 MHz @ 1.60 V** — the difference the soak length makes, and the reason
`TUNE_VERIFY_MS` exists. The exact edge is not known: CoreMark passes at 540 MHz and
fails at 570 MHz at 1.60 V, and the PLL points between (546, 552, 558, 564) have not been
tried. `-DPICO_COPY_TO_RAM=1` does not change the 570 MHz result, so it is not a flash
artifact.

### WeAct RP2350A: clock ceiling ≠ load ceiling

| Point | Result |
| --- | --- |
| 520 MHz, one core, 1.60 V | validates repeatedly, under two compilers |
| 546 MHz, one core | hard fault, PC `0x1000011c`, flash verified byte for byte both times |
| 520 MHz, two cores, one run | validates: 2613.91 iterations/sec |
| 520 MHz, two cores, ten-run soaks | **6, 6, 7 and 5 of 10 runs** under GCC 16.2.0, against 10 of 10 once under GCC 13.2.1 |

The debugger read where two of the short soaks stopped, and they are two failures: the
7-of-10 one was spinning in `core_stop_parallel` — core 0 in `while (!s_core1_done)` with
**no timeout**, CFSR zero, so the run stopped waiting rather than faulted; the 5-of-10 one
left the PC at `0xeffffffe` with CFSR `0x8200` (`PRECISERR` with `BFARVALID`), a precise
bus fault on a non-memory address. The official Pico 2 fails the same way joining core 1
at 546 MHz while the Luckfox board (a 2 A buck-boost supply) does not fail at all at 520
— a supply pattern, not a clock or compiler (the compiler is worth 1.053x on the score
and cannot make an inter-core handshake hang).

It cannot be bought back with voltage: 1.60 V is already the policy ceiling and an
out-of-envelope request is **clamped rather than refused**. Asking for 1.65 V at 520 MHz
left the application at the stock 150 MHz with the divider applied (flash 15 MHz); the
probe's "clock landed where asked" check is what caught it.
`boards/weact_rp2350a.cmake` keeps 520 MHz as the clock ceiling and marks its 520 MHz
`extreme` as a dual-core coin flip. Where the dual-core ceiling is between 400 and
520 MHz is the open measurement.

## RP2040

The official Pico W reached **440 MHz at 1.30 V** and validated 832.21 iterations/sec,
still 1.891 per MHz — with the platform ceiling raised out of the way
(`-D_PLATFORM_MAX_KHZ`). 420 MHz at 1.30 V is the corner of the policy's box (platform
ceiling and regulator documented maximum reached at once); 440 is the board showing there
was margin above it. The AirMech RP2040 board **locked up at 440 MHz**, so on RP2040 the
two boards agree on the voltage each clock needs but not on how far the top voltage goes.
The full voltage-vs-clock table is in [voltage-tiers.md](voltage-tiers.md).

## Derived across platforms

Both platforms are linear in the clock to within 0.001 % (RP2040 1.8914 iterations/sec
per MHz, RP2350 2.8180), so an M33 does **1.490x** the work per clock of an M0+, and the
Pico W's best (440 MHz at 1.30 V) is worth an RP2350 at about **295 MHz**. A Pico 2 at
564 MHz is doing work a Pico W could only match at ~840 MHz, twice past its regulator's
limit. These are *derived*, not measured.

## Not measured yet

- The Luckfox board's 546, 552, 558 and 564 MHz points, and a dual-core soak above
  520 MHz on the official Pico 2.
- The Pico W's true top: 440 MHz passed; 460 and 480 MHz have not been tried.
- Anything in RISC-V mode.
- The WeAct dual-core ceiling between 400 and 520 MHz (see above).

## Sources

- CoreMark repository `RANKINGS.md` and `rpi-pico/MEASUREMENTS.md`; probe logs under
  `coremark/probe-*/`.
