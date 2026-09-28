# Board config: Raspberry Pi Pico 2 (RP2350, 4 MB QSPI flash)
#
# The official board carries a Winbond W25Q32FV/JV (measured: id = 0x1640ef,
# 4096 KiB).  This file is keyed on PICO_BOARD=pico2, which clones share, and a
# clone's flash part can be a different one entirely (the Luckfox board here has a
# Puya PY25Q32HB) -- which is why the ceilings below are the conservative ones and
# why, unlike pico_w, this file has no default profile: it describes two boards
# with different flash parts and different supplies.
#
# Profiles:
#   cmake -DPICO_BOARD=pico2 -DPICO_TURBO_PROFILE=turbo ..
#
#   safe    — 300 MHz, 1.20 V, DIV=6  (flash  50 MHz)
#   fast    — 400 MHz, 1.30 V, DIV=8  (flash  50 MHz)
#   turbo   — 520 MHz, 1.60 V, DIV=10 (flash  52 MHz)   <- both cores, soaked
#   extreme — 564 MHz, 1.60 V, DIV=10 (flash 56.4 MHz)  <- ONE core only
#
# Measured, single core, on the official board (the Luckfox board agrees at the
# clocks it was run at: 300, 400, 520, 540):
#
#   150 MHz  1.10 V   422.71 it/s
#   300 MHz  1.20 V   845.41
#   400 MHz  1.30 V  1127.21
#   520 MHz  1.60 V  1465.38      and 2622.08 with two cores, 50 runs, all validated
#   546 MHz  1.60 V  1538.64      546 with two cores: passed once, hung once (marginal)
#   552 MHz  1.60 V  1555.56
#   558 MHz  1.60 V  1572.47
#   564 MHz  1.60 V  1589.37      564 with two cores hard-faults before it prints
#   570 MHz          does not run (verified: the flash matched the build and the
#                    core was in isr_hardfault)
#
# So the chips stop in the same place on both boards -- 564 passes, 570 fails -- and
# the second core costs headroom: 520 MHz is where two cores were soaked, and 546 is
# where two cores stop being reliable (one pass, one hang).  Voltage is stored at
# 1.60 V, the highest this library
# will ask for (see PICO_TURBO_MAX_VREG_VOLTAGE); dual-core 564 might live at 1.65 V,
# which the hardware has and the policy does not, and no measurement here says so.

# The highest frequency the library will let a build ask for: the highest clock
# validated here, so that a search cannot wander into the clock that is known to
# fail.  Overridable from the command line, because probing past a ceiling is the
# whole point of a probe.
if(NOT DEFINED _PLATFORM_MAX_KHZ)
    set(_PLATFORM_MAX_KHZ 564000)
endif()
# Measured: with the divider derived from 133 MHz (DIV 4, so 78 MHz of flash clock
# at 315 MHz) the chip computed wrong answers and locked up; at 52 MHz the same
# search climbed without a single hang.  The RP2350 runs XIP with the divider crt0
# applies from boot2, so the flash clock scales with clk_sys and this number bounds
# it.
#
# 57 MHz is the clone's ceiling, not the official board's: the Luckfox board's flash
# part was solid at 57 and wrong at 78.75, while this board's W25Q32JV took 86.7
# without complaint.  The shared name gets the conservative number.  It is also why
# this number is 57 and not 60: with odd dividers allowed on RP2350 (see above), a
# 60 MHz ceiling derives DIV 9 at 520 MHz = 57.8 MHz, which is *above* what the clone
# was shown to hold.  The even-only rule used to keep the flash clock just under the
# ceiling by accident; with it off, the ceiling has to be the real limit.
if(NOT DEFINED _FLASH_MAX_KHZ)
    set(_FLASH_MAX_KHZ 57000)
endif()
set(_BOOT2_DEFAULT_DIV 2)
# An odd divider is legal here.  The RP2040's boot stage 2 refuses one
# (#error PICO_FLASH_SPI_CLKDIV must be even, in its w25q080 and at25sf128a
# variants), but the RP2350's w25q080 -- the one the official boards build -- only
# checks the maximum, and the RP2350's QMI divider has no even constraint: three
# Waveshare RP2350 boards ship PICO_FLASH_SPI_CLKDIV 3.  (The RP2350's at25sf128a
# variant does check it, so a board using that one needs even dividers.)
set(_FLASH_REQUIRES_EVEN OFF)

if(PICO_TURBO_PROFILE STREQUAL "safe")
    set(PICO_TURBO_SYS_CLK_KHZ 300000)
    set(PICO_TURBO_VREG_VOLTAGE VREG_VOLTAGE_1_20)
elseif(PICO_TURBO_PROFILE STREQUAL "fast")
    set(PICO_TURBO_SYS_CLK_KHZ 400000)
    set(PICO_TURBO_VREG_VOLTAGE VREG_VOLTAGE_1_30)
elseif(PICO_TURBO_PROFILE STREQUAL "turbo")
    # The configuration to hand an application with two cores: 520 MHz was soaked
    # for 50 consecutive dual-core runs on this board (2622.08 iterations/sec,
    # 0.0002% spread) and 36 on the clone.
    set(PICO_TURBO_SYS_CLK_KHZ 520000)
    set(PICO_TURBO_VREG_VOLTAGE VREG_VOLTAGE_1_60)
elseif(PICO_TURBO_PROFILE STREQUAL "extreme")
    # Single core.  Two cores at this clock hard-fault before printing anything on
    # the official board, and 546 with two cores is already marginal (one pass, one
    # hang) -- so an application that starts the second core wants "turbo".
    set(PICO_TURBO_SYS_CLK_KHZ 564000)
    set(PICO_TURBO_VREG_VOLTAGE VREG_VOLTAGE_1_60)
elseif(NOT PICO_TURBO_PROFILE STREQUAL "")
    message(FATAL_ERROR "pico-turbo: unknown profile '${PICO_TURBO_PROFILE}' for board pico2")
endif()
