# Board config: Raspberry Pi Pico (RP2040, w25q16 flash)
#
# Profiles:
#   cmake -DPICO_BOARD=pico -DPICO_TURBO_PROFILE=turbo ..
#
#   safe    — 240 MHz, stock voltage, DIV=2  (flash 120 MHz)
#   turbo   — 360 MHz, 1.20 V,       DIV=4  (flash  90 MHz)
#   extreme — 400 MHz, 1.30 V,       DIV=4  (flash 100 MHz)

set(_PLATFORM_MAX_KHZ 420000)
set(_BOOT2_DEFAULT_DIV 2)
set(_FLASH_REQUIRES_EVEN ON)

if(PICO_TURBO_PROFILE STREQUAL "safe")
    set(PICO_TURBO_SYS_CLK_KHZ 240000)
    # stock voltage, DIV auto
elseif(PICO_TURBO_PROFILE STREQUAL "turbo")
    set(PICO_TURBO_SYS_CLK_KHZ 360000)
    set(PICO_TURBO_VREG_VOLTAGE VREG_VOLTAGE_1_20)
elseif(PICO_TURBO_PROFILE STREQUAL "extreme")
    set(PICO_TURBO_SYS_CLK_KHZ 400000)
    set(PICO_TURBO_VREG_VOLTAGE VREG_VOLTAGE_1_30)
elseif(NOT PICO_TURBO_PROFILE STREQUAL "")
    message(FATAL_ERROR "pico-turbo: unknown profile '${PICO_TURBO_PROFILE}' for board pico")
endif()
