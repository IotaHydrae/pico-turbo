# Voltage tiers: the chip's, not the board's

> Two RP2040 boards from different vendors need the same voltage at each clock, and the
> RP2350 search points sit exactly on the library's table — the tiers belong to the die.
> The board decides how much *flash clock* can go with them, not the voltage.

## RP2040, measured on an official Pico W

CoreMark, one context, iterations scaled at 24 per MHz, divider 4 (105 MHz of flash clock
at 420 MHz — the divider the library derives for an RP2040 board it has no profile for).
Every row `Correct operation validated` and each row's flash read back and compared
first. The voltage column is the library's table followed from the application's own
state line, not from build flags.

| Clock | Voltage applied | Iterations/sec | per MHz | Flash clock |
| --- | --- | --- | --- | --- |
| 125 MHz | 1.10 V (sel 11, stock) | 236.42 | 1.891 | 62.5 MHz (DIV 2, stock) |
| 240 MHz | 1.10 V (sel 11) | 453.93 | 1.891 | 60 MHz |
| 264 MHz | 1.10 V (sel 11) | 499.32 | 1.891 | 66 MHz |
| 300 MHz | 1.20 V (sel 13) | 567.41 | 1.891 | 75 MHz |
| 360 MHz | 1.20 V (sel 13) | 680.90 | 1.891 | 90 MHz |
| 396 MHz | 1.25 V (sel 14) | 748.99 | 1.892 | 99 MHz |
| 420 MHz | 1.30 V (sel 15) | 794.38 | 1.891 | 105 MHz |

Every step landed on a clock this board ran, including 264 MHz — the row just under the
266 MHz boundary where the table drops back to stock voltage. The score is linear at
1.891 iterations/sec per MHz across all seven points with the flash clock rising from 60
to 105 MHz underneath, so this load never leaves the XIP cache in this range. Two cores
at 420 MHz score 1417.30, i.e. 1.784x one core.

Against the AirMech RP2040, which a search fitted on its own: 260 MHz at sel 11,
360 MHz at sel 13, 390 MHz at sel 14, 420 MHz at sel 15 — the same table. Two RP2040
boards, different vendors and flash parts, same voltage per clock.

At the top: 440 MHz at 1.30 V validated 832.21 iterations/sec (1.891 per MHz), and
1.30 V is the highest the RP2040's regulator is documented for. The AirMech board's
440 MHz **locked up**, so the tier table is shared but the top of the ladder is not.

## RP2040, the AirMech board

| Setting | Result |
| --- | --- |
| 260 MHz @ 1.10 V (sel 11) | accepted (tune tier 0) |
| 360 MHz @ 1.20 V (sel 13) | accepted |
| 390 MHz @ 1.25 V (sel 14) | accepted |
| 420 MHz @ 1.30 V (sel 15) | accepted; the panel runs here at DIV 4, flash 105 MHz |
| 440 MHz @ 1.30 V | locks the chip up; the ceiling is between 420 and 440 MHz |

The search walked 130 → 420 MHz in 5 MHz steps: 59 candidates, 34 of them real PLL
points on a 12 MHz reference. On-chip clocks read back at the stock panel configuration:
`clk_sys` 240 MHz (PLL_SYS FBDIV 120, POSTDIV1 6, VCO 1440 MHz), `clk_peri` 240 MHz,
`clk_usb` 48 MHz from PLL_USB (FBDIV 100, POSTDIV 5/5), `clk_ref` from the 12 MHz XOSC,
flash at DIV 2 = 120 MHz, and a 50 MHz panel bus. This board is where the RP2040 column
of the voltage table and the 133 MHz default fallback ceiling come from: 105 MHz of flash
clock has been fine here.

## RP2350, the table confirmed by the search

The Luckfox points sit on the library's RP2350 table: 300 MHz at 1.20 V (sel 13),
380 MHz at 1.30 V (sel 15), 440 MHz at 1.40 V (sel 17), 520 MHz at 1.60 V (sel 19). The
table's upper entries (1.50 V at 500 MHz) have no separate same-board point; 520 MHz and
above used 1.60 V. The full point list is in [clock-ceilings.md](clock-ceilings.md).

## Related

- [clock-ceilings.md](clock-ceilings.md) — how far each board goes
- [flash-ceiling.md](flash-ceiling.md) — what the board adds to the tiers
- [design.md](../design.md) — the table itself
