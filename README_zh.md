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

## 自动探测（auto-tune）

芯片体质不同：能跑到多少取决于晶圆、板子、flash 和温度，所以在一块 Pico 上稳定的频率，
换一块可能根本跑不到。打开探测，让芯片自己回答这个问题：

```cmake
set(PICO_TURBO_SYS_CLK_KHZ 125000)   # 搜索起点（必须本身是稳的）
set(PICO_TURBO_AUTOTUNE 1)           # 从这里往上爬
set(PICO_TURBO_MAX_CLK_KHZ 420000)   # 爬到这个上限
```

`pico_turbo_init()` 之后会按 `PICO_TURBO_STEP_KHZ` 逐档抬高频率，某档失败就把核心电压升一档，
每个候选都跑一遍自校验压力测试，保留最后一个通过的配置。PLL 打不出来的频率点会被**跳过**而不是就近取整。
试过的每个配置都有 trace：

```c
pico_turbo_state_t st = pico_turbo_state();
printf("%lu kHz at sel %u%s\n", (unsigned long)st.sys_clk_khz,
       (unsigned)st.vreg_sel, st.tuned ? " (searched)" : "");

const pico_turbo_step_t *steps;
for (uint32_t i = 0, n = pico_turbo_trace(&steps); i < n; i++) {
        printf("%lu kHz sel %u -> %u\n", (unsigned long)steps[i].khz,
               (unsigned)steps[i].vreg_sel, steps[i].result);
}
```

| 变量 | 默认值 | 说明 |
|---|---|---|
| `PICO_TURBO_AUTOTUNE` | `0` | 不用固定频率，改为搜索最佳频率与电压 |
| `PICO_TURBO_MAX_CLK_KHZ` | 平台上限 | 搜索允许尝试的最高频率 |
| `PICO_TURBO_STEP_KHZ` | `5000` | 频率步进 |
| `PICO_TURBO_STRESS_MS` | `30` | 每个候选的压力测试时长 |

搜索必须放在 `main()` 的最开始、别的事情发生之前，原因是：**挂死**的候选由看门狗兜住 —— 它会复位
芯片让程序重来，此时搜索从 watchdog scratch 寄存器里读出刚写下的候选，记为"挂死"，然后回到上一个
已经通过的配置。连续挂死达到 `PICO_TURBO_MAX_HANGS` 次就放弃，停在起始频率。最佳配置同样记在
scratch 里，所以**暖复位会直接复用**、不再重新搜索；断电重来才会重搜。也可以自己带策略随时调用：
`pico_turbo_autotune(&policy)`。

## 逐芯片的档位表：测一次，写成头文件

搜索回答的是"这颗芯片这次能跑到哪"；`tune` 例子把答案固定下来，生成一个可以直接
放进工程的头文件，里面是可用档位的表：

```bash
export PICO_SDK_PATH=/path/to/pico-sdk
mkdir -p examples/tune/build && cd examples/tune/build
cmake .. && ninja          # Pico 2 用 -DPICO_BOARD=pico2
```

烧进去、打开串口，它会做搜索、对每个档位再做一次长时间复验，然后把文件打印在两个
标记之间：

```text
---8<--- BEGIN pico_turbo_config.h ---8<---
/*
 * pico_turbo: the stable configurations measured on this chip.
 * ...
 * board id : e6:63:...
 * measured : Sep 27 2025 14:52:01
 * tiers    : 4
 */
#define PICO_TURBO_TUNED_COUNT 4u
#define PICO_TURBO_TUNED_SAFE  0u  /* 最慢、电压最低 */
#define PICO_TURBO_TUNED_MAX   3u  /* 这颗芯片的最高档 */
```

把标记之间的内容剪下来存成 `pico_turbo_config.h`，然后这样用：

```c
#include "pico_turbo_config.h"

int main(void) {
        /* 表就是搜索结果，运行时不需要再搜： */
        pico_turbo_use_table(pico_turbo_tuned_configs, PICO_TURBO_TUNED_COUNT);
        pico_turbo_select(PICO_TURBO_TUNED_MAX);   /* 或 SAFE，或任意档位 */
        ...
}
```

这张表描述的是**被测量的那颗芯片**，这正是它的意义：同型号的两块板子上限并不相同，
在一块上测出来的表在另一块上只是猜测。每个档位是"该电压下这颗芯片能稳定跑到的最高
频率"，并且只包含通过了例子自身复验的档位——复验失败的会被剔除并在串口上说明。

几点注意：

* 用这张表的工程必须允许表里的频率：`PICO_TURBO_MAX_CLK_KHZ` 要能覆盖最高档；
  超出它或超出平台稳压范围的配置会被拒绝，而不是照单执行。
* 复验期间把芯片跑挂的档位会被记进 watchdog scratch 寄存器，所以 watchdog 复位后下
  一次启动能把这个复位归到具体那一档，而不是让整次测量白跑。
* `-DTUNE_STRESS_MS`、`-DTUNE_STEP_KHZ`、`-DTUNE_MAX_KHZ`、`-DTUNE_LEAVE_AT_MAX`
  控制搜索强度和结束时停留的档位（默认停在最安全的那一档：停在最高档的芯片更难重新
  烧录）。

| 函数 | 说明 |
|---|---|
| `pico_turbo_use_table(configs, count)` | 把一张配置表交给库 |
| `pico_turbo_tier_count()` | 表里有几个档位 |
| `pico_turbo_tier(i)` | 取一个档位；越界时返回全零配置 |
| `pico_turbo_select(i)` | 应用一个档位；返回频率是否**精确**达到 |
| `pico_turbo_tiers_from_trace(out, max)` | 把上次搜索的 trace 归并成"每个电压一档" |

`pico_turbo_apply()` 自己也是最后一道防线：频率为 0、超过本工程上限、或稳压档位超出
平台范围的配置会被直接拒绝（不动硬件），"升压再升频、降频再降压"的顺序由它保证——
顺序反了会让芯片在低电压下继续跑旧的高频，实测就是锁死、必须断电重插。

## 配置参数

| 变量 | 默认值 | 说明 |
|---|---|---|
| `PICO_TURBO_SYS_CLK_KHZ` | *(空)* | 目标 CPU 频率，单位 kHz（如 `400000` 表示 400 MHz）。留空则不超频。 |
| `PICO_TURBO_FLASH_CLK_DIV` | *(自动)* | Flash SPI 分频系数。留空则自动计算；遇到不稳定的 flash 芯片可手动指定。 |
| `PICO_TURBO_FLASH_MAX_KHZ` | `133000` | 自动分频要把 XIP 时钟压在这个上限以下；flash 芯片慢就调低它。 |
| `PICO_TURBO_VREG_VOLTAGE` | *(自动)* | 核心电压，如 `VREG_VOLTAGE_1_30`。留空则根据频率自动选择。 |

RP2040 可用值：`VREG_VOLTAGE_DEFAULT`、`VREG_VOLTAGE_1_20`、`VREG_VOLTAGE_1_25`、`VREG_VOLTAGE_1_30`。
RP2350 额外可用扩展范围（`VREG_VOLTAGE_1_35` … `VREG_VOLTAGE_3_30`）。

### 命令行传参

```bash
cmake -DPICO_TURBO_SYS_CLK_KHZ=400000 ..
cmake -DPICO_TURBO_SYS_CLK_KHZ=512000 -DPICO_TURBO_FLASH_CLK_DIV=9 ..
cmake -DPICO_TURBO_SYS_CLK_KHZ=400000 -DPICO_TURBO_VREG_VOLTAGE=VREG_VOLTAGE_1_25 ..
```

### CMakeLists.txt 中设置

```cmake
set(PICO_TURBO_SYS_CLK_KHZ 400000)
set(PICO_TURBO_FLASH_CLK_DIV 4)                # 可选，手动覆盖
set(PICO_TURBO_VREG_VOLTAGE VREG_VOLTAGE_1_25) # 可选，手动覆盖
add_subdirectory(libs/pico-turbo)
target_link_libraries(my_app pico_turbo)
```

## 工作原理

`pico_turbo_init()` 按顺序执行三步：

1. **调压** — 根据目标频率提高核心电压（RP2040: 1.10–1.30 V; RP2350: 1.10–1.60 V，已自动解除电压限制）。
2. **稳定** — 等待电压调节器稳定（2 倍 SDK 自己的最低要求 `SYS_CLK_VREG_VOLTAGE_AUTO_ADJUST_DELAY_US`）。这段忙等用 `clock_get_hz(clk_sys)` 换算，也就是**改频之前**正在跑的那个时钟。
3. **时钟** — 重新配置系统 PLL 与 `clk_sys` 至目标频率，并把 `clk_peri` 指向它。
4. **核对** — 回读实际达到的 `clk_sys`，记录是否与请求完全一致（见 `pico_turbo_get_status()`）——电压是按"请求的频率"选的，达不到就得让人知道。

## 平台支持

| 平台 | 最高频率 | Boot2 flash 分频 | 备注 |
|---|---|---|---|
| RP2040 | 420 MHz | 2（必须偶数） | 最大 `VREG_VOLTAGE_1_30` |
| RP2350 | 520 MHz | 4 | 高于 1.30 V 时调用 `vreg_disable_voltage_limit()` |

## Flash 分频器

XIP flash 时钟 = `sys_clk / PICO_FLASH_SPI_CLKDIV`，所以它会跟着核心频率一起涨：分频值必须按
**这个构建能到达的最高频率**来算，而不是按起始频率。库取"让 flash 不超过上限"的最小合法分频值：

- **分频系数必须为偶数**，RP2040 和 RP2350 都一样（两边的 boot stage 2 都有同一句
  `#error PICO_FLASH_SPI_CLKDIV must be even`）
- 上限**按板子给**：`boards/pico2.cmake` 用 60 MHz，RP2040 板子沿用 133 MHz 的 QSPI 接口上限

第二点不是细节。在这块 Pico 2 上实测：520 MHz 构建算出 DIV 4 —— 爬到顶端时 flash 130 MHz，
315 MHz 时就已 78 MHz —— 芯片从那时起开始自检不符并硬故障，看起来**完全像是硅片频率上限**。
换成 DIV 10（52 MHz）后，同样的搜索一路爬到 520 MHz（该窗口的上限）、一次挂死都没有；把代码放进
SRAM 跑（`-DPICO_COPY_TO_RAM=1`）结果相同，这就说明问题在 flash 时钟、不在取指。

所以：把上限压在自己 flash 芯片能接受的范围里，知道自己在做什么时再覆盖 —— 覆盖上限或直接给分频值：

```bash
cmake -DPICO_TURBO_FLASH_MAX_KHZ=55000 ..
cmake -DPICO_TURBO_FLASH_CLK_DIV=9 ..
```

开了 `PICO_TURBO_AUTOTUNE` 时，分频按 `PICO_TURBO_MAX_CLK_KHZ` 计算，而不是按起始频率：它必须对
搜索可能到达的**每一个**候选都安全，否则最先出问题的是 flash。而分频过快是**看不出来**的 —— 直到
从 flash 读回的数据错了，所以自检里专门对镜像的一段做了哈希来抓这种情况。

分频必须在**第一次 XIP 访问之前**就是对的（早于 `main()`），所以它属于 boot stage 2：只要算出来的分频与板子的默认值**不同**，库就会给 `bs2_default` 打补丁 —— 比默认值*小*也要打，否则 flash 会一直跑在比它该有的速度更慢的档位上。注意 `pico_sdk_init()` 要在 `add_subdirectory(pico-turbo)` **之前**调用；若那时还没有 `bs2_default` 目标，补丁会被跳过并给出 CMake 警告。

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
/* 策略：构建时定好的，或按需搜索 */
void pico_turbo_init(void);
pico_turbo_config_t pico_turbo_autotune(const pico_turbo_autotune_t *policy);

/* 机制：自己指定一组频率与电压 */
bool pico_turbo_apply(const pico_turbo_config_t *config);
pico_turbo_state_t pico_turbo_state(void);
uint16_t pico_turbo_voltage_for_khz(uint32_t khz);

/* 自检：这颗芯片还算得对吗？ */
uint32_t pico_turbo_self_test(uint32_t ms);
bool pico_turbo_self_check(uint32_t ms, uint32_t reference);

/* 报告 */
uint32_t pico_turbo_trace(const pico_turbo_step_t **steps);
```

在 `main()` 最开始、`stdio_init_all()` 及任何外设初始化之前调用一次。不超频时该函数为空 — 可在默认频率和超频版本之间共用同一份代码。重复调用不会做第二次。

之后用 `pico_turbo_state()` 看实际结果：

| 字段 | 含义 |
|---|---|
| `enabled` / `tuned` | 是否编进了目标频率；配置是否来自搜索 |
| `reached` | `clk_sys` 是否精确达到请求值 |
| `requested_khz` / `sys_clk_khz` | 请求值 / 当前值 |
| `peri_clk_khz` | `clk_peri`，凡是由它派生的都要跟着改 |
| `usb_clk_khz` / `usb_ok` | 必须保持 48000 —— 它来自独立的 USB PLL |
| `flash_clk_khz` | `clk_sys / PICO_FLASH_SPI_CLKDIV`，也就是 boot2 编进去的那个分频 |
| `vreg_sel` | 稳压器设置，用 SDK 的 `VREG_VOLTAGE_*` 编码表示（**不是** mV） |
| `hangs` | 搜索期间被看门狗复位掉的候选数 |

`pico_turbo_apply()` 是单独的机制层：先电压、等稳压、再改时钟、最后 `clk_peri`。频率若 PLL 无法精确达到，
就**保持原状**而不是就近取整；返回值告诉你是否真的达到。要自己爬阶梯就用 `check_sys_clock_khz()` 先判可实现性。

`pico_turbo_self_test(ms)` 会跑一段固定的自校验整数负载，加上对镜像一段做 XIP 读回哈希，持续 `ms` 毫秒并返回哈希。
这个哈希只与"做了多少活"有关，所以拿它和一颗你信任的芯片上的值比对，就能判断某个频率/电压/flash 时序是否真的安全 ——
能跑但**算错**才是超频最常见的失效方式，而"崩不崩"式的测试抓不到它。

## 调用之后：还有哪些东西必须自己跟上

时钟不会替你照顾别的。凡是**编译期**从外设/系统时钟算出来的时序，现在都错了：

- 用写死的 `clk_sys` 算出来的 PIO 分频（应该用运行期的 `clock_get_hz(clk_sys)` 去除）；
- 写死的 SPI 波特率（用 `spi_init()` 就好，它运行时读 `clk_peri`），或按旧频率调的 PWM wrap；
- 任何按周期校准过的循环。

USB 是例外：`clk_usb` 由独立的 48 MHz PLL 供给，所以 `clk_sys` 怎么变 USB 都照常工作 —— 同样因为这个，超频也**不会**让 USB 变快。

## 宿主机上的单元测试

跟芯片无关的那一半（电压/频率施加顺序、非法配置的拒绝、trace 到档位表的归并、搜索
的边界）不需要硬件就能测：

```bash
make -C test/host        # 编译并运行
```

用 `-Werror -Wall -Wextra` 编译库本身和一堆 pico-sdk 桩，把库对硬件做的每一次调用
记成日志再断言顺序。芯片相关的那一半（这颗 RP2040 到底能不能跑 420 MHz）只能上板测。

## 故障排查

超频后若 Pico 无法启动、卡死或运行不稳定：

0. **先看它到底跑到了多少。** `pico_turbo_state()` 会给出请求值与实际 `clk_sys`、外设/USB/flash 时钟、以及稳压器设置 —— 通常一眼就能看出问题出在频率、电压还是 flash 分频上。
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
