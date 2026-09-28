# Board config: Raspberry Pi Pico W (RP2040, w25q16 flash, wireless)
#
# Profiles:
#   cmake -DPICO_BOARD=pico_w ..                             # defaults to extreme
#   cmake -DPICO_BOARD=pico_w -DPICO_TURBO_PROFILE=turbo ..
#   cmake -DPICO_BOARD=pico_w -DPICO_TURBO_PROFILE=none ..   # stock clocks
#
#   safe    — 240 MHz, stock voltage, DIV=4  (flash  60 MHz)
#   fast    — 300 MHz, 1.20 V,       DIV=4  (flash  75 MHz)
#   turbo   — 360 MHz, 1.20 V,       DIV=4  (flash  90 MHz)
#   extreme — 440 MHz, 1.30 V,       DIV=4  (flash 110 MHz)   <- the default
#
# Every number here was measured on a Pico W with CoreMark, each point written,
# read back and compared before it ran, and accepted only where the chip measured
# itself at the clock it had been asked for.  The voltages are the ones the
# library's own table asks for at those clocks -- 1.10 V (the RP2040 default) to
# 266 MHz, 1.20 V above it, 1.25 V above 360, 1.30 V above 396:
#
#   125 MHz  1.10 V   236.42 it/s   flash  62.5 MHz (DIV 2, the stock divider)
#   240 MHz  1.10 V   453.93        60 MHz
#   264 MHz  1.10 V   499.32        66 MHz
#   300 MHz  1.20 V   567.41        75 MHz
#   360 MHz  1.20 V   680.90        90 MHz
#   396 MHz  1.25 V   748.99        99 MHz
#   420 MHz  1.30 V   794.39       105 MHz
#   440 MHz  1.30 V   832.21       110 MHz
#
# The score is linear at 1.891 iterations/sec per MHz across all of them, with the
# flash clock rising from 60 to 110 MHz underneath, so this load stays inside the
# XIP cache and the divider is not what those numbers are testing.
#
# 440 MHz is what this file defaults to, and it is the point that was soaked: 30
# consecutive dual-core runs at 1.30 V with DIV 4, every one validated, no errors,
# mean 1484.793434 iterations/sec, 0.0009% spread.  It is also the highest voltage
# the RP2040's regulator is documented for, so there is no headroom above it.  The
# AirMech RP2040 board locked up at 440 MHz, so that is a statement about the
# official board rather than about the chip: a clone may need less.

# The highest clock this board validated -- and soaked at.  The RP2040's own
# documented clk_sys maximum is far below any of this; what makes 440 defensible
# here is the measurement above, not the datasheet.
if(NOT DEFINED _PLATFORM_MAX_KHZ)
    set(_PLATFORM_MAX_KHZ 440000)
endif()

# Measured: with this ceiling the derived divider is DIV 2 at 125 MHz and DIV 4
# everywhere above 240 -- the divider every row above was actually run at -- and at
# 440 MHz DIV 4 means 110 MHz of flash clock, which the dual-core soak held.  The
# QSPI interface's own limit is 133 MHz, and a divider is only interesting below
# that.  Overridable from the command line, because probing where a board gives up
# means asking for more than the ceiling.
if(NOT DEFINED _FLASH_MAX_KHZ)
    set(_FLASH_MAX_KHZ 110000)
endif()
set(_BOOT2_DEFAULT_DIV 2)
# The RP2040's boot stage 2 refuses an odd divider outright
# (#error PICO_FLASH_SPI_CLKDIV must be even), so an odd one is not a hint that
# will be rounded -- it is a divider the hardware will not take.
set(_FLASH_REQUIRES_EVEN ON)

# "none" is the way back to stock clocks: a build that wants pico-turbo's mechanism
# or its self-check without being overclocked says so, and this file leaves it alone.
set(_PICO_W_PROFILE_ASKED FALSE)
if(PICO_TURBO_PROFILE STREQUAL "none")
    set(PICO_TURBO_PROFILE "")
    # Remember that this was asked for: clearing the profile makes it look exactly
    # like a caller who said nothing, and the default below would then put the
    # overclock back on the build that just asked to be left alone.  An explicitly
    # given clock is left alone too -- "none" means no profile, not no clock.
    set(_PICO_W_PROFILE_ASKED TRUE)
endif()

# The default.  Asking for nothing gets this board's measured best -- but only when
# nothing was asked for at all: an explicit profile, an explicit clock, or an
# autotuning build all mean the caller has its own opinion about the frequency.
if(NOT _PICO_W_PROFILE_ASKED AND PICO_TURBO_PROFILE STREQUAL ""
   AND PICO_TURBO_SYS_CLK_KHZ STREQUAL "" AND NOT PICO_TURBO_AUTOTUNE)
    set(PICO_TURBO_PROFILE "extreme")
    message(STATUS
        "pico-turbo: pico_w given no profile and no clock, defaulting to its measured "
        "extreme (440 MHz, 1.30 V); -DPICO_TURBO_PROFILE=none for stock clocks")
endif()

if(PICO_TURBO_PROFILE STREQUAL "safe")
    set(PICO_TURBO_SYS_CLK_KHZ 240000)
    # stock voltage
    set(PICO_TURBO_FLASH_CLK_DIV 4)
elseif(PICO_TURBO_PROFILE STREQUAL "fast")
    set(PICO_TURBO_SYS_CLK_KHZ 300000)
    set(PICO_TURBO_VREG_VOLTAGE VREG_VOLTAGE_1_20)
    set(PICO_TURBO_FLASH_CLK_DIV 4)
elseif(PICO_TURBO_PROFILE STREQUAL "turbo")
    set(PICO_TURBO_SYS_CLK_KHZ 360000)
    set(PICO_TURBO_VREG_VOLTAGE VREG_VOLTAGE_1_20)
    set(PICO_TURBO_FLASH_CLK_DIV 4)
elseif(PICO_TURBO_PROFILE STREQUAL "extreme")
    set(PICO_TURBO_SYS_CLK_KHZ 440000)
    set(PICO_TURBO_VREG_VOLTAGE VREG_VOLTAGE_1_30)
    set(PICO_TURBO_FLASH_CLK_DIV 4)
elseif(NOT PICO_TURBO_PROFILE STREQUAL "")
    message(FATAL_ERROR "pico-turbo: unknown profile '${PICO_TURBO_PROFILE}' for board pico_w")
endif()
