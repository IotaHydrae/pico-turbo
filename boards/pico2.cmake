# Board config: Raspberry Pi Pico 2 (RP2350, w25x10cl flash)
#
# Profiles:
#   cmake -DPICO_BOARD=pico2 -DPICO_TURBO_PROFILE=turbo ..
#
#   safe    — 225 MHz, stock voltage, DIV=4  (flash  56 MHz)
#   fast    — 300 MHz, 1.20 V,       DIV=4  (flash  75 MHz)
#   turbo   — 366 MHz, 1.20 V,       DIV=4  (flash  91 MHz)
#   extreme — 512 MHz, 1.60 V,       DIV=10 (flash  51 MHz)

# The highest frequency the library will let a build ask for.  Measured on a
# Pico 2 here: 570 MHz at 1.60 V passes a self-check and CoreMark validates
# its own results there, 600 MHz at 1.60 V hangs, so this is the ceiling the
# silicon was found to have with room to spare -- it is not a promise about
# any other board's.
set(_PLATFORM_MAX_KHZ 600000)
# Measured on a Pico 2 here: with the divider derived from 133 MHz (DIV 4,
# so 78 MHz of flash clock at 315 MHz) the chip computed wrong answers and
# locked up; at 52 MHz it climbed to 570 MHz without a single hang.  The
# RP2350 runs XIP with the divider crt0 applies from boot2, so the flash
# clock scales with clk_sys and this number is what bounds it.
set(_FLASH_MAX_KHZ 60000)
set(_BOOT2_DEFAULT_DIV 4)
# The RP2350's boot stage 2 has the same requirement as the RP2040's
# (#error PICO_FLASH_SPI_CLKDIV must be even), and on RP2350 the divider is
# applied by crt0 running boot2 at startup, so an odd one is not a hint that
# will be rounded -- it is a divider the hardware will not take.
set(_FLASH_REQUIRES_EVEN ON)

if(PICO_TURBO_PROFILE STREQUAL "safe")
    set(PICO_TURBO_SYS_CLK_KHZ 225000)
    # stock voltage, DIV auto
elseif(PICO_TURBO_PROFILE STREQUAL "fast")
    set(PICO_TURBO_SYS_CLK_KHZ 300000)
    set(PICO_TURBO_VREG_VOLTAGE VREG_VOLTAGE_1_20)
elseif(PICO_TURBO_PROFILE STREQUAL "turbo")
    set(PICO_TURBO_SYS_CLK_KHZ 366000)
    set(PICO_TURBO_VREG_VOLTAGE VREG_VOLTAGE_1_20)
elseif(PICO_TURBO_PROFILE STREQUAL "extreme")
    set(PICO_TURBO_SYS_CLK_KHZ 512000)
    set(PICO_TURBO_VREG_VOLTAGE VREG_VOLTAGE_1_60)
    # The divider is left to the auto rule above (60 MHz ceiling, rounded up to
    # even: DIV 10, 51 MHz of flash clock).  This profile used to pin DIV 9, which
    # is odd -- and an odd divider is not a hint that gets rounded, it is a
    # configure-time error, so this profile could not be used at all.
elseif(NOT PICO_TURBO_PROFILE STREQUAL "")
    message(FATAL_ERROR "pico-turbo: unknown profile '${PICO_TURBO_PROFILE}' for board pico2")
endif()
