# pico_turbo tune example
#
# Standalone: only built when invoked as the top-level project, not when
# pico-turbo is pulled in via add_subdirectory().
if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    return()
endif()

# Build:
#   mkdir build && cd build
#   cmake .. && ninja            # pico, probing 125..420 MHz
#   cmake -DPICO_BOARD=pico2 ..  # pico 2 (the window scales with the platform)
#
# Then open the serial console (115200) and read the generated header between
# the BEGIN/END markers.

cmake_minimum_required(VERSION 3.13)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(pico_turbo_tune C CXX ASM)

set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -Wl,--print-memory-usage")

pico_sdk_init()

# Search by default: this example exists to measure, not to run fast.
if(NOT DEFINED PICO_TURBO_SYS_CLK_KHZ)
    set(PICO_TURBO_SYS_CLK_KHZ 125000)
endif()
if(NOT DEFINED PICO_TURBO_MAX_CLK_KHZ)
    if(PICO_BOARD STREQUAL "pico2")
        set(PICO_TURBO_MAX_CLK_KHZ 300000)
    else()
        set(PICO_TURBO_MAX_CLK_KHZ 420000)
    endif()
endif()
if(NOT DEFINED PICO_TURBO_AUTOTUNE)
    set(PICO_TURBO_AUTOTUNE 1)
endif()
# A slower, more convincing search than a normal boot would do.
if(NOT DEFINED PICO_TURBO_STRESS_MS)
    set(PICO_TURBO_STRESS_MS 100)
endif()

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../.. ${CMAKE_BINARY_DIR}/pico_turbo)

add_executable(tune tune.c)
target_link_libraries(tune pico_turbo pico_stdlib)

pico_enable_stdio_usb(tune 1)
pico_enable_stdio_uart(tune 1)
pico_add_extra_outputs(tune)
