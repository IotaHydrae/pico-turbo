# pico_turbo — Pico overclocking library
#
# Usage (from a parent Pico SDK project):
#   add_subdirectory(path/to/pico-turbo)
#   target_link_libraries(your_app pico_turbo)
#
# Then call pico_turbo_init() at the start of main().
#
# ── Board profile (recommended) ──
#   cmake -DPICO_BOARD=pico2 -DPICO_TURBO_PROFILE=turbo ..
#
# ── Manual frequency ──
#   cmake -DPICO_TURBO_SYS_CLK_KHZ=400000 ..
#   cmake -DPICO_TURBO_SYS_CLK_KHZ=400000 -DPICO_TURBO_FLASH_CLK_DIV=6 ..
#   cmake -DPICO_TURBO_SYS_CLK_KHZ=400000 -DPICO_TURBO_VREG_VOLTAGE=VREG_VOLTAGE_1_25 ..

# --- defaults --------------------------------------------------------
if(NOT DEFINED PICO_TURBO_PROFILE)
    set(PICO_TURBO_PROFILE "")
endif()
if(NOT DEFINED PICO_TURBO_SYS_CLK_KHZ)
    set(PICO_TURBO_SYS_CLK_KHZ "")
endif()
if(NOT DEFINED PICO_TURBO_FLASH_CLK_DIV)
    set(PICO_TURBO_FLASH_CLK_DIV "")
endif()
if(NOT DEFINED PICO_TURBO_VREG_VOLTAGE)
    set(PICO_TURBO_VREG_VOLTAGE "")
endif()

# ===================================================================
# Load board config  (sets platform constraints + profile overrides)
# ===================================================================

# Load board-specific config if available (uses the official PICO_BOARD name).
if(DEFINED PICO_BOARD)
    set(_BOARD_FILE "${CMAKE_CURRENT_LIST_DIR}/boards/${PICO_BOARD}.cmake")
    if(EXISTS "${_BOARD_FILE}")
        include("${_BOARD_FILE}")
    endif()
endif()

# Fallback: auto-detect platform constraints if no board config was loaded.
if(NOT DEFINED _PLATFORM_MAX_KHZ)
    if(NOT DEFINED PICO_PLATFORM)
        set(PICO_PLATFORM "rp2040")
    endif()
    if(PICO_PLATFORM MATCHES "^rp2040")
        set(_PLATFORM_MAX_KHZ 420000)
        set(_BOOT2_DEFAULT_DIV 2)
        set(_FLASH_REQUIRES_EVEN ON)
    elseif(PICO_PLATFORM MATCHES "^rp2350")
        set(_PLATFORM_MAX_KHZ 520000)
        set(_BOOT2_DEFAULT_DIV 4)
        set(_FLASH_REQUIRES_EVEN OFF)
    else()
        message(FATAL_ERROR "pico-turbo: unknown platform ${PICO_PLATFORM}")
    endif()
endif()

# ===================================================================
# Nothing to do — overclocking disabled
# ===================================================================

if(PICO_TURBO_SYS_CLK_KHZ STREQUAL "")
    add_library(pico_turbo STATIC pico_turbo.c)
    target_include_directories(pico_turbo PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
    target_link_libraries(pico_turbo PUBLIC pico_stdlib hardware_clocks hardware_vreg)
    message(STATUS "pico-turbo: disabled (stock clocks)")
    return()
endif()

# ===================================================================
# Overclocking enabled — validate
# ===================================================================

if(PICO_TURBO_SYS_CLK_KHZ GREATER _PLATFORM_MAX_KHZ)
    message(FATAL_ERROR "pico-turbo: ${PICO_TURBO_SYS_CLK_KHZ} kHz exceeds max ${_PLATFORM_MAX_KHZ} kHz")
endif()

# --- flash divider: auto or manual ---
if(PICO_TURBO_FLASH_CLK_DIV STREQUAL "")
    math(EXPR _DIV "(${PICO_TURBO_SYS_CLK_KHZ} + 132999) / 133000")
    if(_DIV LESS 2)
        set(_DIV 2)
    endif()
    if(_FLASH_REQUIRES_EVEN)
        math(EXPR _REM "${_DIV} % 2")
        if(_REM GREATER 0)
            math(EXPR _DIV "${_DIV} + 1")
        endif()
    endif()
else()
    set(_DIV ${PICO_TURBO_FLASH_CLK_DIV})
    if(_DIV LESS 2)
        message(FATAL_ERROR "pico-turbo: flash DIV must be ≥ 2")
    endif()
    if(_FLASH_REQUIRES_EVEN)
        math(EXPR _REM "${_DIV} % 2")
        if(_REM GREATER 0)
            message(FATAL_ERROR "pico-turbo: RP2040 requires even flash DIV")
        endif()
    endif()
endif()

# --- bump boot2 flash divider if needed ---
if(TARGET bs2_default AND _DIV GREATER _BOOT2_DEFAULT_DIV)
    target_compile_definitions(bs2_default PRIVATE PICO_FLASH_SPI_CLKDIV=${_DIV})
endif()

# ===================================================================
# Build the library
# ===================================================================

add_library(pico_turbo STATIC pico_turbo.c)
target_include_directories(pico_turbo PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
target_link_libraries(pico_turbo PUBLIC pico_stdlib hardware_clocks hardware_vreg)
target_compile_definitions(pico_turbo PUBLIC
    PICO_TURBO_ENABLED=1
    PICO_TURBO_SYS_CLK_KHZ=${PICO_TURBO_SYS_CLK_KHZ}
    PICO_FLASH_SPI_CLKDIV=${_DIV}
    PICO_EMBED_XIP_SETUP=1
)

if(NOT PICO_TURBO_VREG_VOLTAGE STREQUAL "")
    target_compile_definitions(pico_turbo PUBLIC
        PICO_TURBO_VREG_VOLTAGE=${PICO_TURBO_VREG_VOLTAGE})
endif()

# one-line summary
math(EXPR _MHZ "${PICO_TURBO_SYS_CLK_KHZ} / 1000")
math(EXPR _FLASH_MHZ "${PICO_TURBO_SYS_CLK_KHZ} / ${_DIV} / 1000")
if(NOT PICO_TURBO_VREG_VOLTAGE STREQUAL "")
    message(STATUS "pico-turbo: ${_MHZ} MHz, flash DIV ${_DIV} (${_FLASH_MHZ} MHz), ${PICO_TURBO_VREG_VOLTAGE}")
elseif(DEFINED PICO_BOARD AND NOT PICO_TURBO_PROFILE STREQUAL "")
    message(STATUS "pico-turbo: ${PICO_BOARD}/${PICO_TURBO_PROFILE} — ${_MHZ} MHz, flash DIV ${_DIV} (${_FLASH_MHZ} MHz)")
else()
    message(STATUS "pico-turbo: ${_MHZ} MHz, flash DIV ${_DIV} (${_FLASH_MHZ} MHz)")
endif()
