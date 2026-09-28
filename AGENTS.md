# AGENTS.md

本仓库的工作规则，供 AI agent（以及人）在改动前先读一遍。

**实测数据在 [`docs/measurements.md`](docs/measurements.md)**：本文只写"必须遵守的约束"
和入口，不重复数字。

---

## 铁律

1. **未经明确指令，不要 `git commit`，更不要 `git push`。** 改完先报告改了什么、验证到
   什么程度，等指令。
2. **仓库内不得出现内网/个人信息**：本机绝对路径（`/home/...`）、内网 IP、口令、板子序列号。
   文档里用 `<pico-sdk>`、`<this repo>` 这类占位符。
3. **提交前 `make -C test/host` 必须全过**（无需板子）。库的策略面（施加顺序、配置校验、
   trace→档位、搜索边界）在宿主机上有测试；改了这些行为就要加/改对应的测试，
   并且**做一次变异验证**（把修复回退掉，确认测试会红）。
4. **偶数分频是 RP2040 的要求，不是 RP2350 的**（2026-09 从 SDK 源码核对过）：
   RP2040 的 `boot2_w25q080`/`boot2_at25sf128a` 里有 `#error PICO_FLASH_SPI_CLKDIV must be
   even`，而 **RP2350 的 `boot2_w25q080` 只查上限、不查奇偶**（Waveshare 三块 RP2350 板就写
   `PICO_FLASH_SPI_CLKDIV 3`；RP2350 的 `at25sf128a` 变体才查）。所以 `boards/pico2.cmake`
   是 `_FLASH_REQUIRES_EVEN OFF`，RP2040 的两个 board 文件是 ON。上限**按板子给**
   （`boards/*.cmake` 的 `_FLASH_MAX_KHZ`），不是按接口极限。
   顺带记住 SDK 的**默认分频是 2**（两个平台的 `boot2_w25q080` 都 `#define ... 2`，
   at25sf128a 是 4；官方 `pico`/`pico_w`/`pico2` 三个板头文件也都写 2）。
5. **`pico_turbo_apply()` 的施加顺序不许合并成一种**：上升沿先调压再调频，下降沿先降频
   再调压。反过来会在低电压下继续跑旧高频 —— 实测就是锁死、要断电。
6. **只要已验证的结论**。文档里的每个数字要写清来自哪块板、哪个构建；推测显式标"未验证"。

## 提交与身份

- `user.name` = `Wooden Chair`，`user.email` = `hua.zheng@embeddedboys.com`
- 一律带 `Signed-off-by`：`git commit -s`
- 内核风格提交信息：`模块: 简述`，正文写清改了什么、为什么、实测到什么程度；一个逻辑改动
  一个提交

## 构建与运行

```bash
make -C test/host                       # 宿主机测试，37 项，不需要板子

export PICO_SDK_PATH=<pico-sdk>
cmake -S examples/tune -B build -DPICO_BOARD=pico2 \
      -DPICO_TURBO_DIR=<this repo> \
      -DPICO_TURBO_MAX_CLK_KHZ=600000 -DPICO_TURBO_FLASH_MAX_KHZ=55000
cmake --build build -j
```

`-DPICO_BOARD=pico_w` **不带其它参数就是这块板的默认档**（board 文件默认 440 MHz/1.30 V/DIV 4，
configure 时会打出来）；要原厂频率用 `-DPICO_TURBO_PROFILE=none`；自己给档位或给时钟都优先。
这是目前唯一带默认档的 board 文件（理由：官方板实测过、且 440 MHz 已是 RP2040 稳压器的文档上限）。

**分工**：找到极限和稳定性的活儿在 coremark 仓库（它是测试台），结果以 `boards/*.cmake`
的形式回流到本仓库 —— 本仓库的 board 文件就是那些测量的落点。凡在那边测出的稳定配置，
都要在这里有对应的档位/上限/默认值，否则应用拿不到它。

整块板子的"一键体检"在 coremark 仓库：`coremark/tools/probe.py --board <板名>`——时钟阶梯、
flash 分频阶梯、双核、soak 全跑一遍，产出报告和一份可用的 `boards/<板名>.cmake`（本仓库
`boards/pico_w.cmake` 就是这么来的）。接上探针即可，不需要按键。

例子旋钮（都在 `examples/tune/CMakeLists.txt`）：`TUNE_BASE_KHZ`（设成等于上限就是单点
测试）、`TUNE_MIN_VREG_SEL`/`TUNE_MAX_VREG_SEL`（电压窗口）、`TUNE_MAX_HANGS`（一次墙值
几次复位）、`TUNE_VERIFY_MS` + `PICO_TURBO_STRESS_MS`（**这两个决定"通过"的含义**）。

烧写与读取：**优先纯 USB**（`picotool load -v -x --vid 0x2e8a --pid 0x000f`；应用态可以先
`picotool reboot --vid 0x2e8a --pid 0x0009 -f -u` 让它自己进 BOOTSEL ✓）—— 但这条要求应用
真的提供了 USB 复位接口（纪律 5），拿不准时用调试器的擦—写—回读路径（纪律 3/4），
调试器同时负责定位卡点。

## 硬件纪律（都是踩出来的，别省）

1. **批量前先跑一个点**，逐项确认：①构建 ✓ ②烧写后**回读 flash 与构建产物比对** ✓
   ③拿到日志/结果 ✓ ④状态里的**实测时钟 = 请求时钟** ✓（不可达的 PLL 点会"时钟没动"，
   看着像不稳定 ✗）。四项齐了再开循环 —— 曾经整批死在烧写上白等半小时。
2. **`openocd program ... verify` 可能报 "Verified OK" 而只写了一部分** ✗。要么先
   `flash erase_sector` 再写，要么用 picotool，并且**回读比对**才算数。
3. **只有调试器（探针没接复位线、应用也没有 USB 复位接口）时的完整烧写路径**：
   `init` → `halt` → `flash erase_sector 0 0 last` → `flash write_image <elf>` →
   `verify_image <elf>` → `dump_image` 回读比对 → `reset run` 收尾。注意 `program` 命令
   内部要先复位，**没有复位线就直接失败**（`Unable to reset target`）；而
   `verify_image` 在 RP2350 上会因 M33 CRC 算法报错（"error executing cortex_m crc
   algorithm"）却仍可能给出结论 ⇒ **只有回读比对能当证据**。
4. **擦空 flash 就是没有按键时的 BOOTSEL** ✓：`flash erase_sector 0 0 last` 之后 RP2350
   自己会以 `2e8a:000f` 出现在 `lsusb` 里（实测两次），于是 picotool 又能用了。探针没有
   复位线（`SWCLK/TCK = 1 SWDIO/TMS = 1 ... nRESET = 0`）、板子又卡在一个跑完的 app 上
   时，这是唯一不靠手的回头路。**但请注意顺序**：擦完必须马上写回镜像，别把空板留在那儿。
5. **别假设应用一定提供 USB 复位接口** ✗：`picotool reboot --vid 0x2e8a --pid 0x0009 -f -u`
   只在应用把它编进去时才有效（实测 CoreMark 的镜像只会回 "no 0009 to reset"，而
   `examples/tune` 的镜像可以）。启动方式选**确定能成**的那条：调试器 `reset run`，
   或 bootrom 的 `picotool reboot --... --pid 0x000f`。
6. **调试会话结束时核不能留在 halt** ✗：核停了，bootrom 的 USB 也不上线，板子会从
   `lsusb` 整个消失，下一次烧写报 "no accessible RP-series devices"。会话必须以
   `reset run`/`resume` 收尾。
7. **日志读取器要在烧写之前启动** ✓，否则抓到的是上一个应用的残留输出，看起来像这次成了。
8. **过快的 flash 分频会写进 boot2，从而变成"软件复位救不回来"的砖** ✗（实测：官方 Pico 2
   在 520 MHz 配 DIV 4 = 130 MHz flash，核进 lockup，只有按 BOOTSEL 才回来）。
   测分频阶梯时手边要够得着 BOOTSEL。
9. **擦写之后不要 `resume`，要 `reset run`** ✗：刚被擦掉的正是当前在执行的代码，`resume`
   等于让旧应用跑进一片擦空区域（读到 0xFF 就取指异常 → HardFault 挂住，板子的 USB 也
   跟着消失）。"别把核留在 halt"的正确做法是**在同一个 openocd 会话里以 `reset run`
   收尾**：擦 → 写 → `verify_image` → 回读比对 → `reset run`，一步到位，既启动了新镜像
   又不留下 halt 状态。
10. **读取器的 USB 握手必须重试** ✗：SDK 只根据 CDC 的 line coding + DTR 判断"有没有主机"，
   没有主机时**写出去的东西直接丢**。实测一次 `[Errno 110] Operation timed out` 就让 30 次
   soak 变成一篇空日志 —— 空日志不是"慢"，是没人打招呼。握手要重试，而且在**还没收到任何
   数据**时应当反复重申（应用的等待窗口只有 `PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS`）。
11. **`pkill -f` 的模式别匹配到自己** ✗：命令行里带 `pkill -f "pico-console.py"` 就会把执行
   这条命令的 shell 一起杀掉（实测三次，其中一次还把 runner 的 `-u` 参数漏掉、旧 reader
   没被杀掉，新 reader 于是 `Resource busy` 拿不到设备）。用 `pkill -f "名字[.]py"` 这种
   正则技巧，或者直接按 PID 杀。
11b. **探针没有复位线时，两个平台的 `reset run` 都是 vectreset** ✗：它只改 PC。RP2040 上
   它让 SSI 留在非读模式（XIP 读出错位数据，应用 double fault）；RP2350 上它偶发地把
   boot2→crt0 的第一次交接打成 **INVSTATE**（CFSR `0x01020001`，故障 PC 在 `platform_entry`），
   而 flash 逐字节正确、`reset run` 还报成功 —— 现场看起来就是"这块板子在 150 MHz 都会挂"。
   可靠路径是**经 bootrom 的真复位**：调试器擦空（芯片自己进 BOOTSEL：RP2040 `0003`、
   RP2350 `000f`）→ `picotool load -v` 写入 → bootrom 状态下回读比对（比对前要 `resume`，
   停住的核会把 bootrom 的 USB 带走）→ `picotool reboot` 真复位。`coremark/tools/probe.py`
   就是按这条实现的。

12. **（历史）调试器会话必须"真复位 + 释放核"，两句都要** ✗ —— 这条记着 A/B/C 对照的结果，
    但"看门狗触发 + `reset run`"的实现已被纪律 11b 的 bootrom 复位路径取代（那个组合在
    RP2350 上是竞态）。实测（A/B/C 对照，同一 ELF、同一
    板子，只改收尾三行）：
    - **只有 `halt` 没有 `reset run`** ⇒ 写完什么都不会跑（核被 debug 的 halt 咬住）✗ ——
      这是 probe 脚本第一版失败的直接原因。
    - **有 `reset run`** ⇒ 正常出分 ✓。
    - 另有一次带 `reset run` 仍不出分：PC `0xfffffffe`（double fault）、XIP 读出的数据像
      **被移动过**（`00 b5 32 4b` 读成 `00 0b 53 24`，整条流右移一个 nibble），而写入和
      `verify_image` 全部正常 ⇒ **回读 flash（`dump_image`）会把 SSI 留在非读模式，而
      vectreset 修不回来**（RP2040 没有复位线时 `reset run` 只改 PC、不复位外设）。
      实测能救回来的是**真芯片复位**：擦空 → 芯片自己进 BOOTSEL（`2e8a:0003`）→ picotool
      写入+重启 ✓。快照差异见 `coremark/tools/probe.py` 的注释。
    当时的收尾是"清 `SCRATCH4` → 写看门狗 `CTRL` TRIGGER → **`reset run`**"；**现在用的是
    11b 那条**（擦空 → bootrom → picotool 写入 → 回读 → bootrom 真复位），因为看门狗触发
    与随后的 `reset run` 在 RP2350 上是竞态。
13. **调试器是定位工具，不要为了"纯 USB"而放弃它**：它直接给出卡在哪个函数（例如
   `core_list_find` = 应用其实在跑；`isr_hardfault` = 真的挂了；PC 在 bootrom = 镜像没起来）。

## 当前已知边界（详见 docs/measurements.md）

| 板子 | CPU 上限 | flash |
| --- | --- | --- |
| 幸狐 Pico 2（RP2350A，Puya PY25Q32HB 4 MB） | 564 过、570 挂 | ≤57 MHz 稳、78.75 MHz 出错；上限未定位 |
| 官方 Pico 2（RP2350A A2，W25Q32JV 4 MB） | 单核 564 过、570 挂（已核实）；**双核 520 过（50 次 soak），546 一过一挂（边缘）、564 hard fault** | DIV 4（130 MHz）锁死；**104 与 109.2 MHz 通过、112.8 MHz hard fault** ⇒ 上限在 109.2–112.8 MHz；奇数分频实测可用 ✓ |
| 合宙 RP2040 | 420 过、440 锁死 | 105 MHz（DIV 4）长期正常 |
| 官方 Pico W（RP2040 B2） | **440 过（1.30 V；双核 30 次 soak 全 validated）；460 未测** | DIV 4 @440 = 110 MHz 撑住 30 次 soak；@420 = 105 MHz；DIV 2（210 MHz）如预期 hang（越过接口极限） |

两块 RP2350 板在**同一个地方**停住 ⇒ ≈565–570 MHz 是芯片边界；而 flash 上限明显不同
⇒ 那是**板子/flash 芯片**的事，不是 RP2350 接口的事。

RP2040 侧是同样的结论、更强的证据：官方 Pico W 走一遍 125→420 MHz，**每个时钟需要的电压
与合宙板上一轮搜索自己拟合出来的档位完全一致**（260/360/390/420 → sel 11/13/14/15）。
两块不同厂、不同 flash 的板子给出同一张电压表 ⇒ 分档是**芯片**的，板子决定的是能配多快的
flash 时钟。

但**顶端不一样**：合宙板 440 MHz 锁死，官方 Pico W 在 440 MHz/1.30 V 通过并给出 832.21 it/s。
RP2350 那对板子是同停同止（564 过、570 挂），RP2040 这对不是 —— 所以"两块板停在同一处"是两次
测量、不是定律：电压分档可以跨板一致，顶端到哪儿各有各的余量。

## 待办

- 幸狐板的整套 flash 分频阶梯（现在只有"≤57 稳、78.75 出错"两个点）。
- Pico W 真正的顶端：440 已过（1.30 V），460/480 未测 —— 用 `--points 460000,480000 --allow-above-ceiling` 继续。
- 运行时改 SSI 分频：现在分频按构建上限算，导致 520 MHz 的构建即使在 150 MHz 也把 flash
  压到 15 MHz（`SSI_BAUDR` 可以运行时写，只会更慢不会更快）。
- **启动时自己测 flash 分频**：分频上限是**板子**的（克隆板和官方板共用
  `PICO_BOARD=pico2`），按板名给默认值对大厂板偏保守、对克隆板又不够；自检已经会回读镜像，
  同一套机制可以二分出安全分频。（已定：等各板数据齐了再做。）
- trace 上限 64 条：长爬升会被截断，可能让**生成的头文件漏掉最高档**（应当增量维护档位表）。
