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
| Official Pico W | RP2040 B2 | 2 MB (its own part) | measured: see below |

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

### The 520 MHz row again, on both cores, fifty times

The library's job is to land a configuration the chip can hold, and the way that
claim gets tested is a long run rather than a fast one: CoreMark with one context
per core, 34666 iterations each, 520 MHz at 1.60 V, and the divider at 8 for a
65 MHz flash clock -- then fifty consecutive runs, each rebooting through the
bootrom so no run inherits a warm chip.

| Quantity | Result |
|---|---|
| Runs that produced a score | 50 of 50 |
| Runs that validated | 50 of 50 |
| `Errors detected` | 0 |
| Iterations/sec | mean 2622.081163, min 2622.078177, max 2622.083036 |
| Spread | 0.0002% -- 30 distinct scores in 50 runs |
| Clock state lines reading 520000 kHz asked, configured and measured | 50 of 50 |

So the 520 MHz / 1.60 V tier is not a decision the search gets away with once: it
survives fifty dual-core load-and-reboot cycles without a single run losing its
result.  The Luckfox board's equivalent soak (36 runs, 2612.7 iterations/sec) is
0.36% slower and its spread is 0.39% rather than 0.0002% -- the same chip and the
same firmware, so what differs is the board's power supply, and it shows up as
jitter well before it shows up as failure.  A search that has to choose a tier
under a supply like that is choosing with less margin than the score alone
suggests.

The flash ladder on this board: a fixed 520 MHz core at 1.60 V, each point
written and read back before it was run.

| Divider | Flash clock | Result |
|---|---|---|
| 4 | 130 MHz | locks the chip up (PC 0xeffffffe); only the BOOTSEL button brings it back |
| 6 | 86.7 MHz | validated, 1465.39 iterations/sec |
| 8 | 65 MHz | validated, 1465.39 |
| 10 | 52 MHz | validated, 1465.38 |

So this board's flash ceiling is between 86.7 and 130 MHz, while the Luckfox board
above failed at 78.75 MHz and was solid at 57 MHz.  Two RP2350A boards, the same
chip revision, and the flash ceiling differs by more than a factor of two: it is
the board's flash part and layout, not the QSPI interface.  (Which is also why the
question "why is the RP2350's flash slower than the RP2040's" was the wrong
question -- the comparison that produced it was between two boards from different
vendors, and the chips were never the variable.)

That also means the per-board default (60 MHz for `PICO_BOARD=pico2`) is
conservative for an official Pico 2 and necessary for a clone, and clones share
the board name.  This number cannot be keyed on the board name; measuring the
divider at startup -- the self-check already reads a region of the image back, so
the mechanism is there -- is the fix worth having.

## Pico W (RP2040 B2, 2 MB flash)

The RP2040 half of the same question -- are the tiers the chip's or the board's --
asked on the one board here that came from the vendor.  CoreMark, one context,
iterations scaled at 24 per MHz, divider 4 (105 MHz of flash clock at 420 MHz: the
divider this library derives for an RP2040 board it has no profile for), every row
`Correct operation validated` and every row's flash read back and compared first.

| Clock | Voltage applied | Iterations/sec | per MHz | Flash clock |
|---|---|---|---|---|
| 125 MHz | 1.10 V (sel 11, stock) | 236.42 | 1.891 | 62.5 MHz (DIV 2, stock) |
| 240 MHz | 1.10 V (sel 11) | 453.93 | 1.891 | 60 MHz |
| 264 MHz | 1.10 V (sel 11) | 499.32 | 1.891 | 66 MHz |
| 300 MHz | 1.20 V (sel 13) | 567.41 | 1.891 | 75 MHz |
| 360 MHz | 1.20 V (sel 13) | 680.90 | 1.891 | 90 MHz |
| 396 MHz | 1.25 V (sel 14) | 748.99 | 1.892 | 99 MHz |
| 420 MHz | 1.30 V (sel 15) | 794.38 | 1.891 | 105 MHz |

The voltage column is the library's table followed from the application's own
state line, not from the build flags, and every step of it landed on a clock this
board ran -- including 264 MHz, the row just under the 266 MHz boundary where the
table drops back to stock voltage.  420 MHz at 1.30 V is the corner of the box the
policy defines for RP2040 (the platform ceiling and the regulator's documented
maximum, both reached at once) -- but not this board's limit: with the ceiling
raised out of the way (`-D_PLATFORM_MAX_KHZ`, which the board files now allow from
the command line) the same board ran **440 MHz at 1.30 V** and validated 832.21
iterations/sec, still 1.891 per MHz.

That is worth reading next to the AirMech board below, whose 440 MHz *locked up*.
So on RP2040 the two boards agree on the voltage each clock needs -- the tiers are
the chip's -- and disagree about how far the top voltage goes: the official board
took 440 MHz where the clone stopped.  The RP2350 pair did not behave like that
(both stop at 564 passing, 570 failing), which is a reminder that "two boards stop
in the same place" was two measurements and not a law.

Against the AirMech board below, which a search fitted on its own: 260 MHz at sel
11, 360 MHz at sel 13, 390 MHz at sel 14, 420 MHz at sel 15.  Two RP2040 boards,
different vendors, different flash parts, different everything except the silicon,
and the voltage each clock needs comes out the same.  That is the RP2040 version of
the answer the two RP2350 boards gave: the tiers are the chip's, and the board is
what decides how much *flash clock* can go with them.

The score is linear at 1.891 iterations/sec per MHz across all seven points, with
the flash clock rising from 60 to 105 MHz underneath, so this load never leaves the
XIP cache in this range.  Two cores at 420 MHz score 1417.30, i.e. 1.784x one
core -- the same 1.78x the RP2350 boards show.

`boards/pico_w.cmake` carries these numbers -- the platform ceiling, the 105 MHz
flash ceiling and the three profiles (240/360/420 MHz at stock/1.20/1.30 V), each
of which derives the divider the row above was measured at -- and was checked
against this board rather than against itself: a build asking only for
`-DPICO_BOARD=pico_w -DPICO_TURBO_PROFILE=extreme` came up at 420000 kHz measured,
vreg sel 15 and a 105 MHz flash clock, and validated 794.39 iterations/sec.

Soaked at 420 MHz with both cores, 20160 iterations each and a bootrom reboot
between runs: 27 consecutive complete runs, every one validated, no errors, mean
1417.301508 iterations/sec with a 0.0059% spread and every run's 28.449 s within
1.7 ms of the next.  That tier is the corner of the box this library defines for
RP2040 -- the platform's clock ceiling and the regulator's documented maximum
reached at once -- and it holds under both cores for as long as this board was
asked to hold it.

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

- The Pico W above 420 MHz: 440 MHz locked the AirMech RP2040 up, so whether
  the official board does the same is the RP2040 counterpart of the 570 MHz
  question.
- 546, 552, 558 and 564 MHz on the Luckfox board, and a dual-core soak above
  520 MHz on the official one.
- The flash divider ladder on the Luckfox board (only "solid at 57 MHz, wrong at
  78.75 MHz" is known) -- and both ladders' middle points want re-running with the
  self-check reading the image back, which is the mechanism a runtime divider
  would use.
- Anything in RISC-V mode, and any flash part other than the two boards above.

## Method notes, because these have produced wrong answers

- **A log written while no host has the port open is discarded, not buffered**, so
  a reader that starts late can report a run that never happened -- or miss one
  that did.  Attach the reader first, and judge a repeated run by the counter the
  application prints rather than by the lines that arrived.
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
