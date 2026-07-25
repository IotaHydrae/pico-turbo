# Board config: Raspberry Pi Pico 2 (RP2350, w25x10cl flash)
#
# Profiles:
#   cmake -DPICO_BOARD=pico2 -DPICO_TURBO_PROFILE=turbo ..
#
#   safe    — 225 MHz, stock voltage, DIV=4  (flash  56 MHz)
#   fast    — 300 MHz, 1.20 V,       DIV=4  (flash  75 MHz)
#   turbo   — 366 MHz, 1.20 V,       DIV=4  (flash  91 MHz)
#   extreme — 512 MHz, 1.60 V,       DIV=9  (flash  57 MHz)

set(_PLATFORM_MAX_KHZ 520000)
set(_BOOT2_DEFAULT_DIV 4)
set(_FLASH_REQUIRES_EVEN OFF)

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
    set(PICO_TURBO_FLASH_CLK_DIV 9)
elseif(NOT PICO_TURBO_PROFILE STREQUAL "")
    message(FATAL_ERROR "pico-turbo: unknown profile '${PICO_TURBO_PROFILE}' for board pico2")
endif()
