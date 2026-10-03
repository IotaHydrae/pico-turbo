# AGENTS.md

## Skills（本仓遵守）

本仓的一切工作遵循工作区 `../AGENTS.md` 约定的四份 skill。**摘要随仓携带**（离线可读），
完整版在工作区 `skills/`。

| skill | 本仓副本 | 一句话 |
| --- | --- | --- |
| Repository Exploration | [`skills/developer-repository-exprolation/Summary.md`](skills/developer-repository-exprolation/Summary.md) | 先理解再修改；证据优先于直觉 |
| Knowledge | [`skills/developer-knowledge/Summary.md`](skills/developer-knowledge/Summary.md) | 首屏结论、事实分级、信息预算、漂移检查 |
| Testing | [`skills/developer-testing/Summary.md`](skills/developer-testing/Summary.md) | tests/tools 分层、oracle 声明、退出码、N 次测量 |
| Code Quality | [`skills/developer-code-quality/Summary.md`](skills/developer-code-quality/Summary.md) | **能跑 ≠ 完成**；可读性有硬标准 |

### 动手前的四行闸门（**强制**）

改任何代码或配置**之前**先写出这四行 ✓。**第 1 行或第 4 行写不出来就停手** ✗ —— 那是在猜 ✗。

```text
已验证：<确认了什么，凭据是什么：代码/实测/构建日志>
仍未知：<还没确认的；不许用推测填空>
最小改动：<只改一处，为什么是这一处>
生效验证：<如何证明改动真的生效：探针 / grep 生成物 / 构建日志里的编译行>
```

**先确认仪器，再相信读数** ✓ —— 宏没被注入、文件没被编译、配置被 defconfig 覆盖，
这三件事的症状都是"结果莫名其妙" ✗。


> 本文件只写 pico-turbo **特有**的铁律、架构不变量与构建/验证入口。知识库与测试的通用约定
> （文档结构、信息预算、事实分级、oracle 声明、CLI/退出码、命名、**文档↔代码漂移检查**）
> 以工作区根 [`../AGENTS.md`](../AGENTS.md) 为准，这里不再重复。
> 知识库在 [`docs/`](docs/README.md)，实测数据在 [`docs/measurements.md`](docs/measurements.md)；
> 本文只写"必须遵守的约束"和入口，不重复数字。

## 铁律

1. **未经明确指令，不要 `git commit`，更不要 `git push`。** 改完先报告改了什么、验证到什么
   程度，等指令。
2. **仓库内不得出现内网/个人信息**：本机绝对路径、内网 IP、口令、代理地址、板子序列号。
   文档里用 `<pico-sdk>`、`<this repo>`、`<board-id>` 这类占位符。
3. **提交前 `make -C test/host` 必须全过**（无需板子，两个套件）。库的策略面（施加顺序、
   配置校验、trace→档位、搜索边界）在宿主机上有测试；**改了这些行为就要加/改对应测试，并且
   做一次变异验证**（把修复回退掉，确认测试会红）。
4. **偶数分频是 RP2040 的要求，不是 RP2350 的**：RP2040 的 `boot2_w25q080`/`boot2_at25sf128a`
   有 `#error PICO_FLASH_SPI_CLKDIV must be even`；**RP2350 的 `w25q080`（官方板用的那个）
   只查上限、不查奇偶**（Waveshare 三块 RP2350 板就写 `PICO_FLASH_SPI_CLKDIV 3`；RP2350 的
   `at25sf128a` 变体才查）。所以 `boards/pico2.cmake` 是 `_FLASH_REQUIRES_EVEN OFF`，RP2040
   的两个 board 文件是 ON。flash 上限**按板子给**（`boards/*.cmake` 的 `_FLASH_MAX_KHZ`），
   不是按 133 MHz 接口极限。SDK 默认分频是 2（两个平台的 `boot2_w25q080` 都写 2，
   at25sf128a 是 4；官方 `pico`/`pico_w`/`pico2` 三个板头文件也都写 2）。
5. **`pico_turbo_apply()` 的施加顺序不许合并成一种**：上升沿先调压再调频，下降沿先降频再调压。
   反过来会在低电压下继续跑旧高频 —— 实测就是锁死、要断电。
6. **只要已验证的结论**。文档里的每个数字要写清来自哪块板、哪个构建，以及编译器
   （见 [`docs/measurements/toolchain-validity.md`](docs/measurements/toolchain-validity.md)）；
   推测显式标"未验证"。

## 提交与身份

- `user.name` = `Wooden Chair`，`user.email` = `hua.zheng@embeddedboys.com`
- 一律带 `Signed-off-by`：`git commit -s`
- 内核风格提交信息：`模块: 简述`，正文写清改了什么、为什么、实测到什么程度；一个逻辑改动
  一个提交

## 构建与验证入口

```bash
make -C test/host                       # 全部离线验证：C 套件 37 项 + turbocfg 套件 90 项

tools/turbocfg.py board-show boards/pico_w.cmake
tools/turbocfg.py config-explain --platform rp2040 --khz 440000 \
    --board-file boards/pico_w.cmake --verbose

export PICO_SDK_PATH=<pico-sdk>
cmake -S examples/tune -B build -DPICO_BOARD=pico2 \
      -DPICO_TURBO_DIR=<this repo> \
      -DPICO_TURBO_MAX_CLK_KHZ=600000 -DPICO_TURBO_FLASH_MAX_KHZ=55000
cmake --build build -j
```

- `make -C test/host` = `check-c`（库 + `test/host/stub/`，`-Wall -Wextra -Werror`，把每次
  硬件调用记成日志再断言顺序）+ `check-tools`（`tools/turbocfg.py`）。退出码 0 PASS / 1 FAIL；
  没有 usage/environment 面，其余退出码不臆造。每个 PASS/FAIL 都在运行日志里声明
  oracle 类型/来源/期望值，见 [`docs/testing.md`](docs/testing.md)。
- `-DPICO_BOARD=pico_w` **不带其它参数就是这块板的默认档**（board 文件默认 440 MHz/1.30 V/
  DIV 4，configure 时会打出来）；要原厂频率用 `-DPICO_TURBO_PROFILE=none`；自己给档位或给时钟
  都优先。这是目前唯一带默认档的 board 文件（理由：官方板实测过、且 440 MHz 已是 RP2040
  稳压器的文档上限）。
- 例子旋钮（`examples/tune/CMakeLists.txt`）：`TUNE_BASE_KHZ`（设成等于上限就是单点测试）、
  `TUNE_MIN_VREG_SEL`/`TUNE_MAX_VREG_SEL`（电压窗口）、`TUNE_MAX_HANGS`（一次墙值几次复位）、
  `TUNE_VERIFY_MS` + `PICO_TURBO_STRESS_MS`（**这两个决定"通过"的含义**）。

**分工**：找到极限和稳定性的活儿在 `coremark` 仓库（它是测试台），结果以 `boards/*.cmake`
的形式回流到本仓库 —— 本仓库的 board 文件就是那些测量的落点。凡在那边测出的稳定配置，都要
在这里有对应的档位/上限/默认值，否则应用拿不到它。整块板子的"一键体检"是
`coremark/tools/probe.py --board <板名>`：时钟阶梯、flash 分频阶梯、双核、soak 全跑一遍，
产出报告和一份可用的 `boards/<板名>.cmake`（本仓库 `boards/pico_w.cmake` 就是这么来的）。
`tools/turbocfg.py board-propose` 在离线侧做同一件事的"提案"环节。

## 架构不变量

1. **flash 分频属于 boot stage 2**：分频要按 `PICO_TURBO_MAX_CLK_KHZ`（搜索可能到达的最高点）
   推导，不是按起始频率；算出来与 `_BOOT2_DEFAULT_DIV` 不同就打在 `bs2_default` 上 —— 比默认
   值*小*也要打，否则 flash 一直跑得过慢。`pico_sdk_init()` 必须在
   `add_subdirectory(pico-turbo)` **之前**，否则补丁跳过并给警告。细节见
   [`docs/flash-divider.md`](docs/flash-divider.md)。
2. **配置校验是最后一道防线**：`config_valid()` 拒绝 khz==0、khz > `PICO_TURBO_MAX_CLK_KHZ`、
   `vreg_sel` > `PICO_TURBO_MAX_VREG_SEL`，且**不动硬件**。`VREG_VOLTAGE_*` 是寄存器编码，
   越界值会落进电压选择字段低位、要到断电才能恢复；这是实测出来的，不要放松。
3. **超出策略上限的电压请求是 clamp 而不是拒绝**：`PICO_TURBO_MAX_VREG_VOLTAGE` = RP2040
   1.30 V / RP2350 1.60 V。实测请求 1.65 V @520 MHz 会静默停在 stock 150 MHz + 已应用分频，
   只有状态行看得出来。改上限要有测量支撑。
4. **trace→档位的语义**：`pico_turbo_tiers_from_trace()` 取"每个电压下最高的稳定频率"，按爬升
   顺序输出，entry 0 是起始配置。trace 上限 `PICO_TURBO_TRACE_STEPS`（64）条，超出即截断 ——
   长爬升可能让生成的档位表漏掉最高档，档位表应当增量维护（见待办）。
5. **看门狗 scratch**：tuner 用 slot 0..3（魔数 `'ptun'`、inflight、best、hangs），slot 4 是
   SDK 的 `watchdog_caused_reboot()`，slot 6 留给调试器读"从哪里 resume"。改动这块前读
   [`docs/autotune.md`](docs/autotune.md)。
6. **`PICO_TURBO_RESOLVED_*` 是给调用方看的**：用 board 档位构建时 `PICO_TURBO_SYS_CLK_KHZ`
   是空的，CoreMark 端曾据此把工作量按 125 MHz 算、实际跑在 420 MHz，被自己的十秒规则判无效。
   改 CMake 的解析逻辑要保持这四个变量正确。

## 硬件纪律（都是踩出来的，别省）

1. **批量前先跑一个点**，逐项确认：①构建 ✓ ②烧写后**回读 flash 与构建产物比对** ✓
   ③拿到日志/结果 ✓ ④状态里的**实测时钟 = 请求时钟** ✓（不可达的 PLL 点会"时钟没动"，
   看着像不稳定 ✗）。四项齐了再开循环 —— 曾经整批死在烧写上白等半小时。
2. **`openocd program ... verify` 可能报 "Verified OK" 而只写了一部分** ✗。要么先
   `flash erase_sector` 再写，要么用 picotool，并且**回读比对**才算数。
3. **只有调试器（探针没接复位线、应用也没有 USB 复位接口）时的完整烧写路径**：
   `init` → `halt` → `flash erase_sector 0 0 last` → `flash write_image <elf>` →
   `verify_image <elf>` → `dump_image` 回读比对 → `reset run`。`program` 内部要先复位，
   没有复位线就直接失败（`Unable to reset target`）；RP2350 上 `verify_image` 会因 M33 CRC
   算法报错却仍给结论 ⇒ **只有回读比对能当证据**。（当前自动化路径见 11b。）
4. **擦空 flash 就是没有按键时的 BOOTSEL** ✓：`flash erase_sector 0 0 last` 之后 RP2350
   自己会以 `2e8a:000f` 出现在 `lsusb` 里（RP2040 是 `2e8a:0003`），于是 picotool 又能用。
   探针没有复位线、板子又卡在一个跑完的 app 上时，这是唯一不靠手的回头路。**擦完必须马上
   写回镜像**，别把空板留在那儿。
5. **别假设应用一定提供 USB 复位接口** ✗：`picotool reboot --vid 0x2e8a --pid 0x0009 -f -u`
   只在应用把它编进去时才有效（实测 CoreMark 镜像只回 "no 0009 to reset"，`examples/tune`
   的可以）。启动方式选**确定能成**的那条：调试器 `reset run`，或 bootrom 的
   `picotool reboot --... --pid 0x000f`。
6. **调试会话结束时核不能留在 halt** ✗：核停了，bootrom 的 USB 也不上线，板子会从 `lsusb`
   整个消失，下一次烧写报 "no accessible RP-series devices"。会话必须以 `reset run`/`resume`
   收尾。
7. **日志读取器要在烧写之前启动** ✓，否则抓到的是上一个应用的残留输出，看起来像这次成了。
8. **过快的 flash 分频会写进 boot2，从而变成"软件复位救不回来"的砖** ✗（实测：官方 Pico 2
   在 520 MHz 配 DIV 4 = 130 MHz flash，核进 lockup，只有按 BOOTSEL 才回来）。测分频阶梯时
   手边要够得着 BOOTSEL。
9. **擦写之后不要 `resume`，要 `reset run`** ✗：刚被擦掉的正是当前在执行的代码，`resume`
   会让旧应用跑进擦空区域（读到 0xFF 就取指异常 → HardFault 挂住，USB 跟着消失）。"别把核
   留在 halt"的正确做法是**在同一个 openocd 会话里以 `reset run` 收尾**。
10. **读取器的 USB 握手必须重试** ✗：SDK 只按 CDC 的 line coding + DTR 判断"有没有主机"，
    没有主机时写出去的东西直接丢。实测一次 `[Errno 110] Operation timed out` 就让 30 次 soak
    变成一篇空日志 —— 空日志不是"慢"，是没人打招呼。没收任何数据时要反复重申握手。
11. **`pkill -f` 的模式别匹配到自己** ✗：用 `pkill -f "名字[.]py"` 这种正则技巧，或直接按 PID 杀。
11b. **探针没有复位线时，两个平台的 `reset run` 都是 vectreset** ✗：它只改 PC。RP2040 上它让
    SSI 留在非读模式（XIP 读出错位数据，应用 double fault）；RP2350 上它偶发地把 boot2→crt0 的
    第一次交接打成 **INVSTATE**（CFSR `0x01020001`，故障 PC 在 `platform_entry`），而 flash
    逐字节正确、`reset run` 还报成功 —— 现场看起来就是"这块板子在 150 MHz 都会挂"。可靠路径是
    **经 bootrom 的真复位**：调试器擦空（芯片自己进 BOOTSEL）→ `picotool load -v` 写入 →
    bootrom 状态下回读比对（比对前要 `resume`，停住的核会把 bootrom 的 USB 带走）→
    `picotool reboot` 真复位。`coremark/tools/probe.py` 就是按这条实现的。
12. **（历史）调试会话必须"真复位 + 释放核"** ✗ —— A/B/C 对照（同一 ELF、同一板子，只改收尾
    三行）证明：只有 `halt` 没有 `reset run` ⇒ 写完什么都不跑；有 `reset run` ⇒ 正常出分；
    另有一次带 `reset run` 仍不出分（PC `0xfffffffe` double fault，XIP 读数整条右移一个
    nibble，而写入与 `verify_image` 正常）⇒ **`dump_image` 会把 SSI 留在非读模式，vectreset
    修不回来**，真芯片复位才能救。当时的"看门狗触发 + `reset run`"实现已被 11b 取代
    （那个组合在 RP2350 上是竞态）。可复用的教训已压缩进
    [`docs/measurements/method-notes.md`](docs/measurements/method-notes.md)。
13. **调试器是定位工具，不要为了"纯 USB"而放弃它**：它直接给出卡在哪个函数（例如
    `core_list_find` = 应用其实在跑；`isr_hardfault` = 真的挂了；PC 在 bootrom = 镜像没起来）。

## 当前已知边界

详细数字与出处见 [`docs/measurements/`](docs/measurements.md) 与
[`docs/boards.md`](docs/boards.md)。一句话版本：

| 板子 | CPU | flash |
| --- | --- | --- |
| 幸狐 Pico 2（RP2350A，Puya PY25Q32HB） | 564 过、570 挂 | ≤57 MHz 稳、78.75 MHz 出错；上限未定位 |
| 官方 Pico 2（RP2350A A2，W25Q32JV） | 单核 564 过、570 挂；双核 520 过（50 次 soak）、546 边缘、564 hard fault | DIV 5 奇数可用；104/109.2 MHz 过、112.8 hard fault ⇒ 上限 109.2–112.8 MHz |
| 合宙 RP2040 | 420 过、440 锁死 | 105 MHz（DIV 4）长期正常 |
| 官方 Pico W（RP2040 B2，W25Q16JV） | 440 过（1.30 V，双核 30 次 soak）；460 未测 | 110 MHz（DIV 4 @440）撑住 30 次 soak |
| WeAct RP2350A（W25Q32JV） | 单核 520 过、546 硬挂；双核 520 边缘（10 次 soak 只跑满 6/6/7/5） | 只用过 52 MHz（DIV 10）；阶梯未跑 |

两块 RP2350 板在同处停住 ⇒ ≈565–570 MHz 是芯片边界；flash 上限差 2 倍以上 ⇒ 那是板子/flash
芯片的事。RP2040 两块板给出同一张电压表 ⇒ 分档是芯片的；顶端各自不同（合宙 440 锁死、官方
440 过）。**"两块板停在同一处"是两次测量，不是定律。**

## 待办

- 幸狐板的整套 flash 分频阶梯（现在只有"≤57 稳、78.75 出错"两个点）。
- Pico W 真正的顶端：440 已过（1.30 V），460/480 未测 —— 用
  `--points 460000,480000 --allow-above-ceiling` 继续。
- WeAct 的双核墙：400–520 之间哪一档双核稳（`--points 480000 --mt 2 --soak 10`）。
- 运行时改 SSI 分频：现在分频按构建上限算，导致 520 MHz 的构建即使在 150 MHz 也把 flash
  压到 15 MHz（`SSI_BAUDR` 可以运行时写，只会更慢不会更快）。
- **启动时自己测 flash 分频**：分频上限是**板子**的（克隆板和官方板共用 `PICO_BOARD=pico2`），
  按板名给默认值对大厂板偏保守、对克隆板又不够；自检已经会回读镜像，同一套机制可以二分出安全
  分频。（已定：等各板数据齐了再做。）
- trace 上限 64 条：长爬升会被截断，可能让**生成的头文件漏掉最高档**（应当增量维护档位表）。

## 驱动工具的方式（与工作区规范同源）

- **不许盲目 `sleep`，不许 blanket 超时** ✓ —— 用**轮询就绪**（0.2 s 间隔）+ **秒级超时** ✓。
  硬件测试必须**显式定义就绪检测**，不要依赖"设备恰好已经跑着" ✓。
- 反例：`sleep 22` + `timeout 300` ⇒ 明明 0.4 s 就有结论的操作拖到几分钟 ✗。
- 正解：`usb.core.find` 轮询 ✓、控制请求 0.5 s 超时 ✓、shell 命令 `timeout 10` ✓。
