# pico_turbo benchmark example
#
# Build (stock):
#   mkdir build && cd build && cmake .. && ninja
#
# Build (overclocked):
#   mkdir build_oc && cd build_oc
#   cmake -DPICO_TURBO_SYS_CLK_KHZ=400000 .. && ninja
#
# Build (overclocked + run from SRAM to bypass flash limit):
#   cmake -DPICO_TURBO_SYS_CLK_KHZ=400000 -DCOPY_TO_RAM=ON .. && ninja
#
# Flash both .uf2 files and compare serial output.

cmake_minimum_required(VERSION 3.13)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(pico_turbo_benchmark C CXX ASM)

# Print memory usage after linking.
set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG}   -Wl,--print-memory-usage")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -Wl,--print-memory-usage")

option(COPY_TO_RAM "Copy firmware to SRAM at boot (bypasses flash XIP limit)" OFF)

pico_sdk_init()

# pico_turbo library (two directories up)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../.. ${CMAKE_BINARY_DIR}/pico_turbo)

add_executable(benchmark main.c)
target_link_libraries(benchmark pico_turbo pico_stdlib)

if(COPY_TO_RAM)
    pico_set_binary_type(benchmark copy_to_ram)
endif()

pico_enable_stdio_usb(benchmark 1)
pico_enable_stdio_uart(benchmark 1)
pico_add_extra_outputs(benchmark)
