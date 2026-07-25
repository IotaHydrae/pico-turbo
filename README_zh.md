# pico_turbo

[English](README.md)

适用于 Raspberry Pi Pico (RP2040 / RP2350) 的轻量级超频库。

添加为 git 子模块，设定目标频率，调用一个函数 — 即可完成超频。

## 快速开始

```bash
# 添加到你的项目中
git submodule add https://github.com/IotaHydrae/pico-turbo.git libs/pico-turbo
```

```cmake
# CMakeLists.txt
set(PICO_TURBO_SYS_CLK_KHZ 400000)        # 目标 400 MHz
add_subdirectory(libs/pico-turbo)
target_link_libraries(your_app pico_turbo)
```

```c
// main.c — 在外设初始化之前调用一次
#include <pico_turbo.h>

int main(void) {
    pico_turbo_init();
    // ...
}
```

如果不设置 `PICO_TURBO_SYS_CLK_KHZ`（或留空），该库为空操作 — 项目将以默认频率编译运行，零开销。

## 配置参数

| 变量 | 默认值 | 说明 |
|---|---|---|
| `PICO_TURBO_SYS_CLK_KHZ` | *(空)* | 目标 CPU 频率，单位 kHz（如 `400000` 表示 400 MHz）。留空则不超频。 |
| `PICO_TURBO_FLASH_CLK_DIV` | *(自动)* | Flash SPI 分频系数。留空则自动计算；遇到不稳定的 flash 芯片可手动指定。 |

### 命令行传参

```bash
cmake -DPICO_TURBO_SYS_CLK_KHZ=400000 ..
cmake -DPICO_TURBO_SYS_CLK_KHZ=512000 -DPICO_TURBO_FLASH_CLK_DIV=9 ..
```

### CMakeLists.txt 中设置

```cmake
set(PICO_TURBO_SYS_CLK_KHZ 400000)
set(PICO_TURBO_FLASH_CLK_DIV 4)           # 可选，手动覆盖
add_subdirectory(libs/pico-turbo)
target_link_libraries(my_app pico_turbo)
```

## 工作原理

`pico_turbo_init()` 按顺序执行三步：

1. **调压** — 根据目标频率提高核心电压（RP2040: 1.10–1.30 V; RP2350: 1.10–1.60 V，已自动解除电压限制）。
2. **稳定** — 等待电压调节器稳定（2 倍 SDK 推荐延迟，以 XOSC 周期精确计量）。
3. **时钟** — 重新配置系统 PLL 至目标频率，并将 `clk_peri` 指向新的 `clk_sys`。

## 平台支持

| 平台 | 最高频率 | Boot2 flash 分频 | 备注 |
|---|---|---|---|
| RP2040 | 420 MHz | 2（必须偶数） | 最大 `VREG_VOLTAGE_1_30` |
| RP2350 | 520 MHz | 4 | 高于 1.30 V 时调用 `vreg_disable_voltage_limit()` |

## Flash 分频器

XIP flash 时钟 = `sys_clk / PICO_FLASH_SPI_CLKDIV`。库会自动计算保证 flash ≤ 133 MHz 的最小合法分频值：

- **RP2040**：分频系数必须为**偶数**（2, 4, 6, …）
- **RP2350**：任意 ≥ 2 的整数均可

极限频率下，简单公式可能不够保守 — 若出现不稳定，手动指定分频值：

```bash
cmake -DPICO_TURBO_FLASH_CLK_DIV=9 ..
```

若计算出的分频值高于 boot2 默认值，库还会同步更新 `bs2_default`，确保从上电第一条指令起 XIP 即可正常工作。

## Benchmark 示例

```bash
cd examples/benchmark

# 默认频率 (125 MHz)
mkdir build && cd build && cmake .. && make -j4

# 超频 (400 MHz)
mkdir build_oc && cd build_oc
cmake -DPICO_TURBO_SYS_CLK_KHZ=400000 .. && make -j4

# 超频 + 从 SRAM 运行（绕过 flash XIP 瓶颈）
cmake -DPICO_TURBO_SYS_CLK_KHZ=400000 -DCOPY_TO_RAM=ON .. && make -j4
```

分别烧录两个 `.uf2` 文件，通过串口终端（115200 波特率）对比输出。Benchmark 运行质数筛和 Mandelbrot 逃逸时间计算，报告微秒级耗时 — 直接反映超频提升幅度。

## API

```c
void pico_turbo_init(void);
```

在 `main()` 最开始、`stdio_init_all()` 及任何外设初始化之前调用一次。不超频时该函数为空 — 可在默认频率和超频版本之间共用同一份代码。

## 故障排查

超频后若 Pico 无法启动、卡死或运行不稳定：

1. **增大 flash 分频系数。** Flash 芯片可能跟不上自动计算的速度。尝试更大的 `PICO_TURBO_FLASH_CLK_DIV` 以降低 flash 工作频率：
   ```bash
   cmake -DPICO_TURBO_FLASH_CLK_DIV=6 ..
   cmake -DPICO_TURBO_FLASH_CLK_DIV=9 ..   # 若仍不稳定
   ```
2. **从 SRAM 运行。** 当 flash 实在无法匹配系统时钟时，在启动时将整个固件拷贝到 SRAM 中执行。这能完全消除 XIP 瓶颈（代价是可用 RAM 减少）：
   ```bash
   cmake -DCOPY_TO_RAM=ON ..
   ```
   或在 CMakeLists.txt 中：
   ```cmake
   pico_set_binary_type(your_app copy_to_ram)
   ```

## ⚠️ 警告

超频会增加功耗和发热，**可能永久损坏你的设备**。使用风险自负。本库按原样提供，不作任何担保。

## 许可证

MIT — 详见 [LICENSE](LICENSE)。
