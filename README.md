# pico_turbo

[中文版本](README_zh.md)

A lightweight overclocking library for Raspberry Pi Pico (RP2040 / RP2350).

Add it as a git submodule, set a target frequency, call one function — done.

## Quick Start

```bash
# Add to your project
git submodule add https://github.com/IotaHydrae/pico-turbo.git libs/pico-turbo
```

```cmake
# In your CMakeLists.txt
set(PICO_TURBO_SYS_CLK_KHZ 400000)        # target 400 MHz
add_subdirectory(libs/pico-turbo)
target_link_libraries(your_app pico_turbo)
```

```c
// In your main.c — call once before any peripheral init
#include <pico_turbo.h>

int main(void) {
    pico_turbo_init();
    // ...
}
```

That's it. If you don't set `PICO_TURBO_SYS_CLK_KHZ` (or leave it empty), the library is a no-op — your project compiles and runs at stock clocks with zero overhead.

## Configuration

| Variable | Default | Description |
|---|---|---|
| `PICO_TURBO_SYS_CLK_KHZ` | *(empty)* | Target CPU clock in kHz (e.g. `400000` for 400 MHz). Empty = no overclock. |
| `PICO_TURBO_FLASH_CLK_DIV` | *(auto)* | Flash SPI clock divider. Leave empty for automatic; set manually for finicky flash chips. |

### Command-line

```bash
cmake -DPICO_TURBO_SYS_CLK_KHZ=400000 ..
cmake -DPICO_TURBO_SYS_CLK_KHZ=512000 -DPICO_TURBO_FLASH_CLK_DIV=9 ..
```

### In CMakeLists.txt

```cmake
set(PICO_TURBO_SYS_CLK_KHZ 400000)
set(PICO_TURBO_FLASH_CLK_DIV 4)           # optional override
add_subdirectory(libs/pico-turbo)
target_link_libraries(my_app pico_turbo)
```

## What It Does

`pico_turbo_init()` performs three steps, in order:

1. **Voltage** — raises core voltage to match the target frequency (RP2040: 1.10–1.30 V; RP2350: 1.10–1.60 V with voltage limit disabled).
2. **Stabilise** — waits for the voltage regulator to settle (2× SDK recommended delay, measured in XOSC cycles).
3. **Clock** — reconfigures the system PLL to the target speed and points `clk_peri` at the new `clk_sys`.

## Platform Support

| Platform | Max clock | Boot2 flash DIV | Notes |
|---|---|---|---|
| RP2040 | 420 MHz | 2 (even only) | `VREG_VOLTAGE_1_30` maximum |
| RP2350 | 520 MHz | 4 | Uses `vreg_disable_voltage_limit()` for > 1.30 V |

## Flash Divider

The XIP flash clock is `sys_clk / PICO_FLASH_SPI_CLKDIV`. The library auto-computes the smallest valid divider that keeps flash ≤ 133 MHz:

- **RP2040**: divider must be **even** (2, 4, 6, …)
- **RP2350**: any integer ≥ 2

At extreme frequencies the simple formula may not be conservative enough — if you see instability, override manually:

```bash
cmake -DPICO_TURBO_FLASH_CLK_DIV=9 ..
```

If the computed divider is higher than the boot2 default, the library also updates `bs2_default` so XIP works from the very first instruction.

## Benchmark Example

```bash
cd examples/benchmark

# Stock (125 MHz)
mkdir build && cd build && cmake .. && make -j4

# Overclocked (400 MHz)
mkdir build_oc && cd build_oc
cmake -DPICO_TURBO_SYS_CLK_KHZ=400000 .. && make -j4

# Overclocked + run from SRAM (bypass flash XIP bottleneck)
cmake -DPICO_TURBO_SYS_CLK_KHZ=400000 -DCOPY_TO_RAM=ON .. && make -j4
```

Flash both `.uf2` files and compare the serial output (115200 baud). The benchmark runs a prime sieve and a Mandelbrot escape-time computation, reporting microseconds elapsed — a direct measure of your overclock gain.

## API

```c
void pico_turbo_init(void);
```

Call once at the very start of `main()`, before `stdio_init_all()` or any peripheral setup. When overclocking is disabled the function is empty — safe to leave the call in place for both stock and turbo builds.

## ⚠️ Warning

Overclocking increases power consumption and heat, and **may permanently damage your device**. Use at your own risk. The library is provided as-is with no warranty.

## License

MIT — see [LICENSE](LICENSE).
