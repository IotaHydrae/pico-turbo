# Flash ceiling

> The safe XIP clock is a property of the board's flash part and layout; two RP2350A
> boards of the same revision bracket it more than a factor of two apart (57 vs
> 109.2–112.8 MHz), while the part is rated 133 MHz.

## Official Pico 2: divider ladder

Fixed 520 MHz core at 1.60 V, each point written and read back before it was run.

| Divider | Flash clock | Result |
| --- | --- | --- |
| 4 | 130 MHz | locks the chip up (PC `0xeffffffe`); only the BOOTSEL button brings it back |
| 5 | 104 MHz | validated at 520 MHz, 1465.40 iterations/sec — an odd divider, which this chip's boot stage 2 accepts |
| 5 | 109.2 MHz | validated at 546 MHz, 1538.66 |
| 5 | 112.8 MHz | hard-faults at 564 MHz |
| 6 | 86.7 MHz | validated, 1465.39 iterations/sec |
| 7 | 74.3 MHz | validated, 1465.39 (odd as well) |
| 8 | 65 MHz | validated, 1465.39 |
| 10 | 52 MHz | validated, 1465.38 |

So this board's flash ceiling is between 109.2 and 112.8 MHz. The part is rated 133 MHz,
so what that brackets is the part *in this configuration* — QMI timing and board layout
included.

## Luckfox Pico 2: divider ladder

The divider is derived from the highest frequency the build can reach, rounded up to the
even number both boot stages require, so the flash clock is bounded for every candidate
the search may try:

| Build ceiling | Divider | Flash clock | Result |
| --- | --- | --- | --- |
| 150 MHz | 4 | 37.5 MHz | fine |
| 400–600 MHz | 10 | up to 57 MHz | fine |
| 520 MHz, `FLASH_MAX_KHZ=55000` | 12 | 43 MHz | fine (also with `-DPICO_COPY_TO_RAM=1`) |
| 300 MHz, default 133 MHz ceiling | 4 | 78.75 MHz at 315 MHz | **self-check mismatches, then hard faults** |

The ceiling of this board's flash is therefore somewhere between 57 and 78.75 MHz. The
part is a Puya PY25Q32HB per Luckfox's own board description; its datasheet clock rating
has not been checked, so whether the window above is the part or the board's layout is
open. The ladder that would locate it (a fixed CPU clock with DIV 4, 6, 8, 10, 12) has
not been run.

## What this settles

Two RP2350A boards, the same chip revision, flash ceilings more than a factor of two
apart → the ceiling is the board's flash part and layout, not the QSPI interface (133 MHz
in both cases). The comparison that produced the question "why is the RP2350's flash
slower than the RP2040's" was between two boards from different vendors, so the chips
were never the variable.

It also means a per-board ceiling keyed on the board name cannot be right for every unit:
`PICO_BOARD=pico2` is shared by the official board (takes 86.7 MHz) and clones (57 MHz
proved on the Luckfox). The current `boards/pico2.cmake` ceiling is 57 MHz, conservative
for an official board and necessary for the clone; a 60 MHz ceiling was considered and
rejected because with odd dividers allowed it derives DIV 9 at 520 MHz = 57.8 MHz, above
what the clone held. This is why a startup measurement of the divider is a wanted feature
(the self-check already reads a region of the image back, so the mechanism exists).

`boards/weact_rp2350a.cmake` carries 52 MHz — DIV 10 at 520 MHz, the fastest this board
has been *shown* to hold. Its ladder has not been run either. With no board file at all
the library falls back to the 133 MHz interface limit and derives DIV 4 at 520 MHz =
130 MHz, the setting measured above as unrecoverable without the button.

## Divider and dual-core load

The divider is worth something only with two cores. On an official Pico 2, 520 MHz
measured 2622.486184 iterations/sec over ten dual-core runs at DIV 8 (65 MHz flash) and
2611.277278 over ten at DIV 10 (52 MHz): 0.43 % slower, with a spread of 0.0271 % against
0.0003 % — ninety times less steady. With one core the same clock reads 1465.38, 1465.39
and 1465.40 at DIV 10, 7 and 5, so 104 MHz of flash clock buys nothing. It is contention
rather than bandwidth, and it is why a row without its divider is not reproducible.

Both dual-core groups above were measured through the same flow in the same session. An
earlier version of this record compared a fifty-run soak with a ten-run one and called
the difference a divider effect; the lesson is in
[method-notes.md](method-notes.md).

## Not measured yet

- The Luckfox board's whole divider ladder (only "solid at 57 MHz, wrong at 78.75 MHz"
  is known).
- Both ladders' middle points want re-running with the self-check reading the image back,
  which is the mechanism a runtime divider would use.
- Any flash part other than the two boards above.

## Related

- [flash-divider.md](../flash-divider.md) — the derivation and the even rule
- [clock-ceilings.md](clock-ceilings.md) — the CPU side
- [soaks.md](soaks.md) — the long runs those divider points were part of
