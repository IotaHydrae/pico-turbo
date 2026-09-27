# Board config: Raspberry Pi Pico W (RP2040, w25q16 flash, wireless)
#
# Profiles:
#   cmake -DPICO_BOARD=pico_w -DPICO_TURBO_PROFILE=turbo ..
#
#   safe    — 240 MHz, stock voltage, DIV=4  (flash  60 MHz)
#   turbo   — 360 MHz, 1.20 V,       DIV=4  (flash  90 MHz)
#   extreme — 420 MHz, 1.30 V,       DIV=4  (flash 105 MHz)
#
# Every number here was measured on a Pico W with CoreMark, single core, each
# point written, read back and compared before it ran, and the voltages are the
# ones the library's own table asks for at those clocks -- 1.10 V (the RP2040
# default) to 266 MHz, 1.20 V above it, 1.25 V above 360, 1.30 V above 396:
#
#   125 MHz  1.10 V   236.42 it/s   flash  62.5 MHz (DIV 2, the stock divider)
#   240 MHz  1.10 V   453.93        60 MHz
#   264 MHz  1.10 V   499.32        66 MHz
#   300 MHz  1.20 V   567.41        75 MHz
#   360 MHz  1.20 V   680.90        90 MHz
#   396 MHz  1.25 V   748.99        99 MHz
#   420 MHz  1.30 V   794.38       105 MHz
#
# The score is linear at 1.891 iterations/sec per MHz across all of them, with the
# flash clock rising from 60 to 105 MHz underneath, so this load stays inside the
# XIP cache and the divider is not being tested by these numbers.  Two cores at
# 420 MHz scored 1417.30 (1.784x one core) and held it for 27 consecutive runs.

# The RP2040's own ceiling, which the SDK's check_sys_clock_khz will not go past
# anyway.  This is the platform's number rather than a measurement: 420 MHz is the
# highest clock tried on this board, and 440 MHz locked an AirMech RP2040 up.
set(_PLATFORM_MAX_KHZ 420000)

# Measured: the ladder above ran with the flash clock following this ceiling
# exactly -- ceil(clk/105) rounded up to even gives DIV 2 at 125 MHz and DIV 4
# everywhere above 240, which is the divider each of those rows was run at.  105
# MHz is the fastest this board has been *shown* to hold; the QSPI interface's own
# limit is 133 MHz, and a divider is only interesting below that.
set(_FLASH_MAX_KHZ 105000)
set(_BOOT2_DEFAULT_DIV 2)
# The RP2040's boot stage 2 refuses an odd divider outright
# (#error PICO_FLASH_SPI_CLKDIV must be even), so an odd one is not a hint that
# will be rounded -- it is a divider the hardware will not take.
set(_FLASH_REQUIRES_EVEN ON)

if(PICO_TURBO_PROFILE STREQUAL "safe")
    set(PICO_TURBO_SYS_CLK_KHZ 240000)
    # stock voltage, DIV auto
elseif(PICO_TURBO_PROFILE STREQUAL "turbo")
    set(PICO_TURBO_SYS_CLK_KHZ 360000)
    set(PICO_TURBO_VREG_VOLTAGE VREG_VOLTAGE_1_20)
elseif(PICO_TURBO_PROFILE STREQUAL "extreme")
    set(PICO_TURBO_SYS_CLK_KHZ 420000)
    set(PICO_TURBO_VREG_VOLTAGE VREG_VOLTAGE_1_30)
elseif(NOT PICO_TURBO_PROFILE STREQUAL "")
    message(FATAL_ERROR "pico-turbo: unknown profile '${PICO_TURBO_PROFILE}' for board pico_w")
endif()
