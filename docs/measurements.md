# What has been measured, on what, and how

The numbers behind the library's defaults -- the flash ceiling, the voltage
table, the platform maximum -- and behind the claims in the READMEs.  They come
from one board each unless a table says otherwise, and they are a record of a
measurement rather than a promise about any other board.

CoreMark results, which is where the stability claims come from, are in the
[CoreMark port's own notes](https://github.com/IotaHydrae/coremark/blob/main/rpi-pico/MEASUREMENTS.md).

## The boards

| Board | Chip | Flash | Notes |
|---|---|---|---|
| Luckfox Pico 2 | RP2350A rev 2, QFN60 | Puya PY25Q32HB, 4 MB QSPI | heatsink fitted; 2 A buck-boost on the 3V3 rail |
| AirMech RP2040 (the PUD panel host) | RP2040 | its own part | the board the RP2040 defaults came from |
| Official Pico 2 | RP2350A | its own part | measured: see below |
| Official Pico W | RP2040 + wireless | -- | connected, not measured yet |

## Luckfox Pico 2 (RP2350A)

### Clock and voltage, from the library's own search

5 MHz steps, 100 ms of stress per candidate, a 200 ms soak when a tier is
verified again.  `sel` is the regulator register encoding: 11 = 1.10 V,
13 = 1.20 V, 15 = 1.30 V, 17 = 1.40 V, 18 = 1.50 V, 19 = 1.60 V, 21 = 1.70 V.

| Setting | Result |
|---|---|
| 300 MHz @ 1.20 V (sel 13) | accepted, and CoreMark validates its results there |
| 380 MHz @ 1.30 V (sel 15) | accepted |
| 440 MHz @ 1.40 V (sel 17) | accepted |
| 520 MHz @ 1.60 V (sel 19) | accepted, and CoreMark validates there (three runs) |
| 570 MHz @ 1.60 V (sel 19) | accepted by this screen -- and CoreMark cannot start there |
| 600 MHz @ 1.60 V (sel 19) | hangs; the watchdog resets the chip and the search comes back |
| 1.70 V (sel 21) | not usable at all: the clock will not move from the base frequency |

With a 2000 ms soak per candidate and a 2000 ms soak per tier instead, the same
search settles at **540 MHz @ 1.60 V**.  That is the difference the soak length
makes, and the reason `TUNE_VERIFY_MS` exists: a 200 ms screen accepted 570 MHz,
where a twelve second CoreMark run cannot bring up its USB.

The exact edge is not known: CoreMark passes at 540 MHz and fails at 570 MHz at
1.60 V, and the PLL points in between (546, 552, 558, 564) have not been tried.
`-DPICO_COPY_TO_RAM=1` does not change the 570 MHz result, so it is not a flash
artifact.

### Flash

The divider is derived from the highest frequency the build can reach, rounded up
to the even number both boot stages require, so the flash clock is bounded for
every candidate the search may try:

| Build ceiling | Divider | Flash clock | Result |
|---|---|---|---|
| 150 MHz | 4 | 37.5 MHz | fine |
| 400-600 MHz | 10 | up to 57 MHz | fine |
| 520 MHz, `FLASH_MAX_KHZ=55000` | 12 | 43 MHz | fine (also with copy_to_ram) |
| 300 MHz, default 133 MHz ceiling | 4 | 78.75 MHz at 315 MHz | **self-check mismatches, then hard faults** |

So the ceiling of this board's flash is somewhere between 57 and 78.75 MHz; the
ladder that would locate it (a fixed CPU clock with DIV 4, 6, 8, 10, 12) has not
been run yet.  The part is a Puya PY25Q32HB per Luckfox's own board description;
its datasheet clock rating has not been checked, so whether the window above is
the part or the board's layout is still open.  Boards: see the CoreMark port for
the same measurement on an official Pico 2, which is the experiment that settles
it.

## Official Pico 2 (RP2350A)

CoreMark, 1.60 V, single core, iterations scaled so each run is ~12 s.  Every row
reports `Correct operation validated`.

| Clock | Iterations/sec | Against 520 MHz |
|---|---|---|
| 520 MHz | 1465.38 | 1.000x |
| 546 MHz | 1538.64 | 1.050x |
| 552 MHz | 1555.56 | 1.062x |
| 558 MHz | 1572.47 | 1.073x |
| 564 MHz | 1589.37 | 1.085x |
| 570 MHz | does not run | -- |

The 570 MHz row is verified rather than inferred: the flash was read back and
matched the build exactly, and the core was found in `isr_hardfault`.  That is the
same boundary the Luckfox board has (564 passes, 570 fails there too, and 540 was
its highest search-accepted tier), so on this evidence the ~565-570 MHz edge is
the silicon rather than the board: two boards from different vendors, different
flash parts and different regulators, stop in the same place.

The flash ladder has one point so far: a 520 MHz build with
`-DPICO_TURBO_FLASH_CLK_DIV=4` (a 130 MHz flash clock) locks this board up -- the
core was found at PC 0xeffffffe -- while DIV 10 (52 MHz) runs the same benchmark
happily.  DIV 6 (86.7 MHz) and DIV 8 (65 MHz) are the points that would locate the
ceiling, and the lockup stopped the walk before they ran.

## AirMech RP2040 (the PUD panel host)

Not a CoreMark board -- the numbers below are from the panel firmware, which is a
long, continuous workload rather than a benchmark.

| Setting | Result |
|---|---|
| 260 MHz @ 1.10 V (sel 11) | accepted (tune tier 0) |
| 360 MHz @ 1.20 V (sel 13) | accepted |
| 390 MHz @ 1.25 V (sel 14) | accepted |
| 420 MHz @ 1.30 V (sel 15) | accepted; the panel runs here at DIV 4, flash 105 MHz |
| 440 MHz @ 1.30 V | locks the chip up; the ceiling is between 420 and 440 MHz |

The search that produced those tiers walked 130 -> 420 MHz in 5 MHz steps: 59
candidates, 34 of them real PLL points on a 12 MHz reference.  On-chip clocks
read back at the stock panel configuration: clk_sys 240 MHz (PLL_SYS FBDIV 120,
POSTDIV1 6, VCO 1440 MHz), clk_peri 240 MHz, clk_usb 48 MHz from PLL_USB
(FBDIV 100, POSTDIV 5/5), clk_ref from the 12 MHz XOSC, flash at DIV 2 = 120 MHz,
and a 50 MHz panel bus.

This board is where the RP2040 column of the voltage table and the 133 MHz
default flash ceiling come from: 105 MHz of flash clock has been fine here, which
is well above what the RP2350 board above tolerates.

## Not measured yet

- The official Pico 2 and Pico W: the same clock sweep and the flash ladder, to
  tell a chip limit from a board limit.
- 546, 552, 558 and 564 MHz on the Luckfox board.
- The flash divider ladder on either board.
- Anything in RISC-V mode, and any flash part other than the two above.

## Method notes, because these have produced wrong answers

- **A frequency the PLL cannot hit exactly is not a failure.**  The SDK leaves the
  clock where it was and reports nothing, so a "failed" point may be a point that
  was never tried: 550 and 560 MHz looked like instability until the run's own
  state line showed the clock had not moved.
- **CoreMark prints "Errors detected" for a run shorter than ten seconds**, which
  its reporting rules require, and that is not a result about the chip.  A fixed
  iteration count that is right at 150 MHz is under ten seconds at 500 MHz; the
  port scales iterations with the clock for this reason.
- **`openocd program ... verify` can report "Verified OK" after writing only part
  of an image** (the rest of the flash left erased), which leaves the board
  running whatever was there before and looks like the new firmware misbehaving.
  Erase first, then program, then read the flash back and compare.
- **Reading the chip's state over SWD halts it**, which pauses the sample watchdog
  (the SDK arms it with pause-on-debug), and openocd's resume of the RP2350 core
  pair can fail, leaving the chip stopped.  The console is the channel that does
  not disturb the measurement; a debug reset after reading is the least bad
  alternative.
- **Verify one point end to end before looping.**  Build it, read the flash back
  and compare, and read the state line the run prints.  A batch started before the
  single-point flow is known to work is minutes of waiting for a failure the first
  point would have shown immediately: that happened twice here, once when a whole
  official-Pico-2 sweep died in the flashing step and once when a ladder point left
  the board locked up.  Put an unreachable-frequency check in front of the loop
  too, because three minutes per point is an expensive way to learn that the PLL
  cannot hit a number.
- **A build whose flash divider is too fast can brick a board past what software
  can fix.**  The divider lives in boot stage 2, so every reset re-runs it and
  fails the same way: on an official Pico 2, DIV 4 at 520 MHz (a 130 MHz flash
  clock) left the core in lockup, unrescuable by any debugger reset, and it needed
  the BOOTSEL button.  Keep the button reachable when testing the ladder.
- **The watchdog scratch registers survive a debug reset but not the rescue-config
  reset**, so a recovery that uses the latter also throws away the search's memory
  of what hung.
- **A debug session that ends with the core halted also takes the bootrom's USB
  down**, so the board disappears from `lsusb` entirely and the next flash finds
  "no accessible RP-series devices".  End every session with a reset or a resume.
- **Attach the console reader before flashing, not after.**  An application that is
  still running from the previous flash answers first otherwise, and the log fills
  with the *last* run's output -- which reads exactly like the current one
  succeeded.
- **A chip in lockup needs a reset before anything can be flashed into it**, and
  openocd's flash driver will say so in its own words ("failed to call reset core
  state").  That is a property of the state the chip is in, not of the tool.
