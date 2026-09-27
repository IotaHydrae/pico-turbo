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
# ── Auto-tuning (recommended for unknown silicon) ──
#   cmake -DPICO_BOARD=pico -DPICO_TURBO_SYS_CLK_KHZ=125000 -DPICO_TURBO_AUTOTUNE=1 ..
#   (searches from 125 MHz up to the platform maximum; see README)
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
# Runtime search for the best frequency/voltage instead of one fixed point.
# PICO_TURBO_SYS_CLK_KHZ becomes the frequency to start from then.
if(NOT DEFINED PICO_TURBO_AUTOTUNE)
    set(PICO_TURBO_AUTOTUNE 0)
endif()
if(NOT DEFINED PICO_TURBO_STEP_KHZ)
    set(PICO_TURBO_STEP_KHZ 5000)
endif()
if(NOT DEFINED PICO_TURBO_STRESS_MS)
    set(PICO_TURBO_STRESS_MS 30)
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

# Ceiling the automatic flash divider keeps the XIP clock under.  133 MHz is the
# QSPI interface's limit, which is not the same thing as what a board's flash
# tolerates: on a Pico 2 measured here, 78 MHz of flash clock produced wrong
# answers and a lockup while 52 MHz was solid, so the board file names its own
# number.  A flash chip that needs less still can have it -- set
# PICO_TURBO_FLASH_CLK_DIV directly and the divider stops being derived.
if(NOT DEFINED PICO_TURBO_FLASH_MAX_KHZ)
    if(DEFINED _FLASH_MAX_KHZ)
        set(PICO_TURBO_FLASH_MAX_KHZ ${_FLASH_MAX_KHZ})
    else()
        set(PICO_TURBO_FLASH_MAX_KHZ 133000)
    endif()
endif()

# How high the *search* may go, i.e. the frequency the flash divider has to be
# safe for: the divider is a build-time constant baked into the boot stage 2, and
# computing it for the starting frequency instead would leave the flash running
# too fast as soon as a candidate above that frequency passes.
if(NOT DEFINED PICO_TURBO_MAX_CLK_KHZ)
    if(PICO_TURBO_AUTOTUNE AND DEFINED _PLATFORM_MAX_KHZ)
        set(PICO_TURBO_MAX_CLK_KHZ ${_PLATFORM_MAX_KHZ})
    else()
        set(PICO_TURBO_MAX_CLK_KHZ ${PICO_TURBO_SYS_CLK_KHZ})
    endif()
endif()
if(PICO_TURBO_AUTOTUNE AND PICO_TURBO_MAX_CLK_KHZ LESS PICO_TURBO_SYS_CLK_KHZ)
    message(FATAL_ERROR
        "pico-turbo: PICO_TURBO_MAX_CLK_KHZ (${PICO_TURBO_MAX_CLK_KHZ}) is below the "
        "starting frequency PICO_TURBO_SYS_CLK_KHZ (${PICO_TURBO_SYS_CLK_KHZ})")
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
    # No target frequency: pico_turbo_init() does nothing.  The mechanism
    # (pico_turbo_apply()) and the self-check are still there for callers that
    # want them, so the sources are built either way.
    add_library(pico_turbo STATIC pico_turbo.c pico_turbo_selftest.c
                                pico_turbo_tune.c)
    target_include_directories(pico_turbo PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
    target_link_libraries(pico_turbo PUBLIC pico_stdlib hardware_clocks
                                             hardware_vreg hardware_watchdog)
    message(STATUS "pico-turbo: disabled (stock clocks)")
    # Defined but empty: no overclocking was asked for, so the SDK's default clock
    # is what runs.  Consumers check the value, not whether it exists.
    set(PICO_TURBO_RESOLVED_CLK_KHZ "" PARENT_SCOPE)
    return()
endif()

# ===================================================================
# Overclocking enabled — validate
# ===================================================================

if(PICO_TURBO_SYS_CLK_KHZ GREATER _PLATFORM_MAX_KHZ)
    message(FATAL_ERROR "pico-turbo: ${PICO_TURBO_SYS_CLK_KHZ} kHz exceeds max ${_PLATFORM_MAX_KHZ} kHz")
endif()

# --- flash divider: auto or manual ---
# It has to hold for the whole range a search may walk, not just for its start.
set(_FLASH_TOP_KHZ ${PICO_TURBO_MAX_CLK_KHZ})
if(PICO_TURBO_FLASH_CLK_DIV STREQUAL "")
    math(EXPR _DIV "(${_FLASH_TOP_KHZ} + ${PICO_TURBO_FLASH_MAX_KHZ} - 1) / ${PICO_TURBO_FLASH_MAX_KHZ}")
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
            message(FATAL_ERROR
                "pico-turbo: this board requires an even flash DIV -- the boot "
                "stage 2 of both RP2040 and RP2350 refuses an odd one")
        endif()
    endif()
endif()

# --- boot2 flash divider ---
# The divider has to be correct from the first XIP access, before main() runs, so
# it belongs to the boot stage 2 and not to anything this library does at runtime.
# A divider *lower* than the board's default matters too: leaving boot2 at 4 while
# the application uses 2 clocks the flash at half of what it could take.
if(TARGET bs2_default)
    if(NOT _DIV EQUAL _BOOT2_DEFAULT_DIV)
        target_compile_definitions(bs2_default PRIVATE PICO_FLASH_SPI_CLKDIV=${_DIV})
    endif()
else()
    message(WARNING
        "pico-turbo: no bs2_default target -- the boot stage 2 keeps its default "
        "flash divider (${_BOOT2_DEFAULT_DIV}), so XIP will run at "
        "${_FLASH_TOP_KHZ}/${_BOOT2_DEFAULT_DIV} kHz. Call "
        "pico_sdk_init() before add_subdirectory(pico-turbo).")
endif()

# ===================================================================
# Build the library
# ===================================================================

add_library(pico_turbo STATIC pico_turbo.c pico_turbo_selftest.c pico_turbo_tune.c)
target_include_directories(pico_turbo PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
target_link_libraries(pico_turbo PUBLIC pico_stdlib hardware_clocks hardware_vreg
                                         hardware_watchdog)
target_compile_definitions(pico_turbo PUBLIC
    PICO_TURBO_ENABLED=1
    PICO_TURBO_SYS_CLK_KHZ=${PICO_TURBO_SYS_CLK_KHZ}
    PICO_FLASH_SPI_CLKDIV=${_DIV}
    # The same number under a name of the library's own: PICO_FLASH_SPI_CLKDIV is
    # the SDK's, and something else in a project can define it last.
    PICO_TURBO_FLASH_DIV=${_DIV}
    PICO_EMBED_XIP_SETUP=1
)
if(PICO_TURBO_AUTOTUNE)
    target_compile_definitions(pico_turbo PUBLIC
        PICO_TURBO_AUTOTUNE=1
        PICO_TURBO_STEP_KHZ=${PICO_TURBO_STEP_KHZ}
        PICO_TURBO_STRESS_MS=${PICO_TURBO_STRESS_MS}
    )
endif()
# The highest frequency this build may apply, which is what the library's own
# sanity check uses to tell a configuration from a corrupt record.  It has to be
# defined in *every* build: a fixed-frequency build whose target is above the
# header's fallback used to have its own target refused.
target_compile_definitions(pico_turbo PUBLIC
    PICO_TURBO_MAX_CLK_KHZ=${PICO_TURBO_MAX_CLK_KHZ}
)

if(NOT PICO_TURBO_VREG_VOLTAGE STREQUAL "")
    target_compile_definitions(pico_turbo PUBLIC
        PICO_TURBO_VREG_VOLTAGE=${PICO_TURBO_VREG_VOLTAGE})
endif()

# one-line summary
math(EXPR _MHZ "${PICO_TURBO_SYS_CLK_KHZ} / 1000")
math(EXPR _FLASH_KHZ "${_FLASH_TOP_KHZ} / ${_DIV}")
math(EXPR _FLASH_MHZ "${_FLASH_KHZ} / 1000")
if(PICO_TURBO_AUTOTUNE)
    message(STATUS "pico-turbo: search ${PICO_TURBO_SYS_CLK_KHZ}..${PICO_TURBO_MAX_CLK_KHZ} kHz "
                   "in ${PICO_TURBO_STEP_KHZ} kHz steps, ${PICO_TURBO_STRESS_MS} ms per candidate; "
                   "flash DIV ${_DIV} (${_FLASH_MHZ} MHz)")
elseif(NOT PICO_TURBO_VREG_VOLTAGE STREQUAL "")
    message(STATUS "pico-turbo: ${_MHZ} MHz, flash DIV ${_DIV} (${_FLASH_MHZ} MHz), ${PICO_TURBO_VREG_VOLTAGE}")
elseif(DEFINED PICO_BOARD AND NOT PICO_TURBO_PROFILE STREQUAL "")
    message(STATUS "pico-turbo: ${PICO_BOARD}/${PICO_TURBO_PROFILE} — ${_MHZ} MHz, flash DIV ${_DIV} (${_FLASH_MHZ} MHz)")
else()
    message(STATUS "pico-turbo: ${_MHZ} MHz, flash DIV ${_DIV} (${_FLASH_MHZ} MHz)")
endif()

# --- what this build resolved to, for whoever added this directory -----------
# The board's profile can supply the clock, so a consumer that reads only the input
# variables (PICO_TURBO_SYS_CLK_KHZ and friends) can see nothing at all and end up
# sizing its own work for the wrong frequency -- which is what happened to a
# CoreMark port whose iteration count is derived from the clock: a profile build
# ran the chip at 420 MHz while the count was computed for 125 MHz, and the run
# came out under CoreMark's own ten second rule.  These are set in the caller's
# scope; the clock is empty when this build asks for no overclocking.
set(PICO_TURBO_RESOLVED_CLK_KHZ ${PICO_TURBO_SYS_CLK_KHZ} PARENT_SCOPE)
set(PICO_TURBO_RESOLVED_MAX_CLK_KHZ ${PICO_TURBO_MAX_CLK_KHZ} PARENT_SCOPE)
set(PICO_TURBO_RESOLVED_FLASH_DIV ${_DIV} PARENT_SCOPE)
set(PICO_TURBO_RESOLVED_FLASH_CLK_KHZ ${_FLASH_KHZ} PARENT_SCOPE)
