# pico_turbo

[English](README.md)

> 适用于 Raspberry Pi Pico (RP2040 / RP2350) 的轻量级超频库：设定目标频率、调用一个函数，
> 电压、PLL 与 flash 分频会随之调整 —— 前提是这些配置都在真板上实测过。

## TL;DR

- 作为子模块加入，设置 `PICO_TURBO_SYS_CLK_KHZ`（如 `400000`），在 `main()` 最开始调用
  `pico_turbo_init()`。不设目标频率时该库为空操作。
- 有实测档位的板子可以带默认值：只写 `-DPICO_BOARD=pico_w` 就会套用它实测的 440 MHz @
  1.30 V，并在 configure 时说明。要原厂频率用 `-DPICO_TURBO_PROFILE=none`；显式给的档位或
  频率优先。
- `PICO_TURBO_AUTOTUNE=1` 让芯片在启动时自己找稳定配置，看门狗兜住挂死的候选；`examples/tune`
  把结果写成逐芯片的档位头文件。
- 任何在**编译期**由 `clk_sys`/`clk_peri` 算出的时序，调用之后都要重算。USB 是例外：它由独立
  的 48 MHz PLL 供给。
- 这里的每个默认值与上限都是一次测量，连同它来自哪块板记录在 [`docs/`](docs/README.md)。

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

要在 `add_subdirectory(pico-turbo)` **之前**调用 `pico_sdk_init()`：flash 分频是在那里补进
boot stage 2 的，没有该 target 时补丁会被跳过并给出警告。

```c
// main.c — 在外设初始化之前调用一次
#include <pico_turbo.h>

int main(void) {
    pico_turbo_init();
    // ...
}
```

## 自动探测（auto-tune）

芯片体质不同：能跑到多少取决于晶圆、板子、flash 和温度。打开探测，让芯片自己回答：

```cmake
set(PICO_TURBO_SYS_CLK_KHZ 125000)   # 搜索起点（必须本身是稳的）
set(PICO_TURBO_AUTOTUNE 1)           # 从这里往上爬
set(PICO_TURBO_MAX_CLK_KHZ 420000)   # 爬到这个上限
```

`pico_turbo_init()` 会按 `PICO_TURBO_STEP_KHZ` 逐档抬高频率，某档失败就把核心电压升一档，
保留最后一个通过的配置。PLL 打不出来的频率点会被**跳过**而不是就近取整。挂死的候选由看门狗
兜住，下一次启动从高它一档的电压继续；暖复位会直接复用已存的结果。`pico_turbo_state()` 报告
找到的配置，`pico_turbo_trace()` 列出试过的每一个。细节见
[docs/autotune.md](docs/autotune.md)。

`examples/tune` 用更严的策略跑同一套搜索、对每个档位再复验一次，然后把
`pico_turbo_config.h` 打印在两个标记之间：一张**在这颗芯片上实测**出来的稳定配置表，交给
`pico_turbo_use_table()` / `pico_turbo_select()`。最高档是候选而不是保证 —— 30 ms 的筛子曾
接受一个 CoreMark 根本起不来的频率。验收要用一段长而杂的负载。

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

`pico_turbo_apply()` 是单独的机制层：上升沿先调压，下降沿先调频，中间等稳压。频率若 PLL
无法精确达到，就**保持原状**而不是就近取整；返回值告诉你是否真的达到。
`pico_turbo_self_test(ms)` 会跑一段固定的自校验负载，加上对镜像一段做 XIP 读回哈希 —— 把它
和一颗你信任的芯片上的值比对，就能判断某个频率/电压/flash 时序是否真的安全，因为"能跑但算错"
才是超频最常见的失效方式。

每个字段与返回值都写在 `include/pico_turbo.h` 里。

## 文档

| 文档 | 内容 |
| --- | --- |
| [docs/README.md](docs/README.md) | 知识库索引与维护约定 |
| [docs/configuration.md](docs/configuration.md) | 全部构建开关、board 档位与解析后的变量 |
| [docs/design.md](docs/design.md) | `init()`/`apply()` 如何工作、安全顺序、调用之后要跟上什么 |
| [docs/flash-divider.md](docs/flash-divider.md) | flash 分频的推导、RP2040 的偶数规则、boot2 补丁 |
| [docs/autotune.md](docs/autotune.md) | 运行时搜索与逐芯片档位头文件 |
| [docs/boards.md](docs/boards.md) | board 文件、档位与上限 |
| [docs/troubleshooting.md](docs/troubleshooting.md) | 跑不起来 / 算错 / 锁死时的排查 |
| [docs/testing.md](docs/testing.md) | 离线宿主机测试、oracle，以及 `tools/turbocfg.py` CLI |
| [docs/measurements.md](docs/measurements.md) | 实测记录索引 |

`examples/benchmark` 是用于对比默认频率与超频的小负载（`-DCOPY_TO_RAM=ON` 让它从 SRAM
运行）；`examples/tune` 就是上面那个测量工具。

## 测试

跟芯片无关的那一半（电压/频率施加顺序、非法配置的拒绝、trace 到档位表的归并、搜索的边界）
不需要硬件就能测：

```bash
make -C test/host        # 编译并运行；0 = 通过，1 = 失败
```

它同时运行 `tools/turbocfg.py`（board 文件与档位表工具）的离线测试。芯片相关的那一半（这颗
RP2040 到底能不能跑 420 MHz）只能上板测。

## ⚠️ 警告

超频会增加功耗和发热，**可能永久损坏你的设备**。使用风险自负。本库按原样提供，不作任何担保。

## 许可证

MIT — 详见 [LICENSE](LICENSE)。
