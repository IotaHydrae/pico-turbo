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
4. **flash 分频必须是偶数**，RP2040/RP2350 的 boot stage 2 都有
   `#error PICO_FLASH_SPI_CLKDIV must be even`；上限**按板子给**（`boards/*.cmake` 的
   `_FLASH_MAX_KHZ`），不是按接口极限。
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
12. **调试器是定位工具，不要为了"纯 USB"而放弃它**：它直接给出卡在哪个函数（例如
   `core_list_find` = 应用其实在跑；`isr_hardfault` = 真的挂了；PC 在 bootrom = 镜像没起来）。

## 当前已知边界（详见 docs/measurements.md）

| 板子 | CPU 上限 | flash |
| --- | --- | --- |
| 幸狐 Pico 2（RP2350A） | 564 过、570 挂 | ≤57 MHz 稳、78.75 MHz 出错；上限未定位 |
| 官方 Pico 2（RP2350A） | 564 过、570 挂（已核实） | DIV 4（130 MHz）锁死；DIV 6（86.7）/8（65）/10（52）全部 validated ⇒ 上限在 86.7–130 MHz 之间 |
| 合宙 RP2040 | 420 过、440 锁死 | 105 MHz（DIV 4）长期正常 |
| 官方 Pico W（RP2040 B2） | 420 过（1.30 V）——420 是平台上限，更高未测 | DIV 4（105 MHz）@420 MHz 通过；再快就越过 QSPI 的 133 MHz 接口极限了 |

两块 RP2350 板在**同一个地方**停住 ⇒ ≈565–570 MHz 是芯片边界；而 flash 上限明显不同
⇒ 那是**板子/flash 芯片**的事，不是 RP2350 接口的事。

RP2040 侧是同样的结论、更强的证据：官方 Pico W 走一遍 125→420 MHz，**每个时钟需要的电压
与合宙板上一轮搜索自己拟合出来的档位完全一致**（260/360/390/420 → sel 11/13/14/15）。
两块不同厂、不同 flash 的板子给出同一张电压表 ⇒ 分档是**芯片**的，板子决定的是能配多快的
flash 时钟。

## 待办

- 幸狐板的整套 flash 分频阶梯（现在只有"≤57 稳、78.75 出错"两个点）。
- Pico W 在 420 MHz 以上（合宙板 440 锁死；官方板是否同样，是 RP2040 版的 570 MHz 问题）。
- 运行时改 SSI 分频：现在分频按构建上限算，导致 520 MHz 的构建即使在 150 MHz 也把 flash
  压到 15 MHz（`SSI_BAUDR` 可以运行时写，只会更慢不会更快）。
- **启动时自己测 flash 分频**：分频上限是**板子**的（克隆板和官方板共用
  `PICO_BOARD=pico2`），按板名给默认值对大厂板偏保守、对克隆板又不够；自检已经会回读镜像，
  同一套机制可以二分出安全分频。（已定：等各板数据齐了再做。）
- trace 上限 64 条：长爬升会被截断，可能让**生成的头文件漏掉最高档**（应当增量维护档位表）。
