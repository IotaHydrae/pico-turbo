# Board config: WeAct Studio RP2350A core board (V1.0), RP2350A rev 2,
# Winbond W25Q32FV/JV 4 MB (JEDEC id 0x1640ef).
#
# Measured, not inherited: one clock ladder, a dual-core point and a ten-run soak
# on this board, every point built, written, read back and compared byte for byte,
# and kept only where the chip measured itself at the clock it had been asked for.
#
#   profiles:
#   cmake -DPICO_BOARD=weact_rp2350a -DPICO_TURBO_PROFILE=turbo ..
#
#   safe    — 300 MHz, the library's voltage for it (1.20 V)
#   fast    — 400 MHz, the library's voltage for it (1.40 V)
#   turbo   — 500 MHz, the library's voltage for it (1.50 V)
#   extreme — 520 MHz, the library's voltage for it (1.60 V)
#
# The four are the four clock-ladder points that validated; no profile here pins a
# voltage or a divider, so what a build gets is what was measured on this board
# rather than this file's opinion of it.  Divider arithmetic stays in the library,
# where the RP2350 boot stage 2's even-number rule lives -- a profile that pinned
# DIV 9 on the Pico 2 could not be configured at all.

# The highest frequency this board validated, at the stock 1.60 V top of the
# library's table: 520 MHz ran single core, ran with both, and ran ten times in a
# row rebooting through the bootrom each time (10 of 10 validated, spread 0.0041%).
# 546 MHz is where it stops, and it stops hard -- the program counter was in
# isr_hardfault and the application never printed its first line.
#
# The official Pico 2 and the Luckfox board both reach 564 MHz, so this is the board
# and not the chip -- which is the whole reason this name exists instead of sharing
# `pico2`.  It is not the compiler either: 546 MHz hard-faults at program counter
# 0x1000011c under GCC 13.2.1 *and* under GCC 16.2.0, with the flash verified byte for
# byte both times.  (The compiler is worth 1.053x on the *score* -- see section 8 of
# the coremark repository's RANKINGS.md -- but it is not what stops this board.)
#
# Overridable from the command line (-D_PLATFORM_MAX_KHZ=546000): probing where a
# board actually gives up means asking for a clock above the ceiling, and that
# should not require editing this file.
if(NOT DEFINED _PLATFORM_MAX_KHZ)
    set(_PLATFORM_MAX_KHZ 520000)
endif()
# The fastest flash clock this board has been *shown* to hold: 52 MHz, which is
# DIV 10 at 520 MHz.  The flash divider ladder has NOT been run on this board, so
# this is where the evidence stops and not where the flash stops -- the part is
# rated 133 MHz and the same part on a Pico 2 was taken to 109 MHz.  Raising it
# means running the ladder, not reading the datasheet.
#
# It matters more than it looks: with no board file at all the library falls back
# to the QSPI interface's 133 MHz, derives DIV 4 at 520 MHz and asks the flash for
# 130 MHz -- the divider this project has already measured as one a board does not
# come back from without the BOOTSEL button.
#
# Overridable from the command line (-D_FLASH_MAX_KHZ=60000).
if(NOT DEFINED _FLASH_MAX_KHZ)
    set(_FLASH_MAX_KHZ 52000)
endif()
# What the SDK's own boot stage 2 would use before this library overrides it: a
# platform constant, not something measured on this board.
set(_BOOT2_DEFAULT_DIV 4)
set(_FLASH_REQUIRES_EVEN ON)

if(PICO_TURBO_PROFILE STREQUAL "safe")
    set(PICO_TURBO_SYS_CLK_KHZ 300000)
    # voltage and divider left to the library's table and its auto rule
elseif(PICO_TURBO_PROFILE STREQUAL "fast")
    set(PICO_TURBO_SYS_CLK_KHZ 400000)
elseif(PICO_TURBO_PROFILE STREQUAL "turbo")
    set(PICO_TURBO_SYS_CLK_KHZ 500000)
elseif(PICO_TURBO_PROFILE STREQUAL "extreme")
    set(PICO_TURBO_SYS_CLK_KHZ 520000)
elseif(NOT PICO_TURBO_PROFILE STREQUAL "")
    message(FATAL_ERROR "pico-turbo: unknown profile '${PICO_TURBO_PROFILE}' for board weact_rp2350a")
endif()

# No default profile, deliberately: naming a board should not silently overclock
# it, and the four points above are 2x to 3.5x the stock clock.  A build that wants
# one asks for it by name.  (boards/pico_w.cmake does carry a default; that was a
# decision for that board, and this one has not been asked.)
