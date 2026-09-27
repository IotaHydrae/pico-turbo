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
`picotool reboot --vid 0x2e8a --pid 0x0009 -f -u` 让它自己进 BOOTSEL ✓），调试器只用于
兜底与定位卡点。

## 硬件纪律（都是踩出来的，别省）

1. **批量前先跑一个点**，逐项确认：①构建 ✓ ②烧写后**回读 flash 与构建产物比对** ✓
   ③拿到日志/结果 ✓ ④状态里的**实测时钟 = 请求时钟** ✓（不可达的 PLL 点会"时钟没动"，
   看着像不稳定 ✗）。四项齐了再开循环 —— 曾经整批死在烧写上白等半小时。
2. **`openocd program ... verify` 可能报 "Verified OK" 而只写了一部分** ✗。要么先
   `flash erase_sector` 再写，要么用 picotool，并且**回读比对**才算数。
3. **调试会话结束时核不能留在 halt** ✗：核停了，bootrom 的 USB 也不上线，板子会从
   `lsusb` 整个消失，下一次烧写报 "no accessible RP-series devices"。会话必须以
   `reset run`/`resume` 收尾。
4. **日志读取器要在烧写之前启动** ✓，否则抓到的是上一个应用的残留输出，看起来像这次成了。
5. **过快的 flash 分频会写进 boot2，从而变成"软件复位救不回来"的砖** ✗（实测：官方 Pico 2
   在 520 MHz 配 DIV 4 = 130 MHz flash，核进 lockup，只有按 BOOTSEL 才回来）。
   测分频阶梯时手边要够得着 BOOTSEL。
6. **调试器是定位工具，不要为了"纯 USB"而放弃它**：它直接给出卡在哪个函数（例如
   `core_list_find` = 应用其实在跑；`isr_hardfault` = 真的挂了；PC 在 bootrom = 镜像没起来）。

## 当前已知边界（详见 docs/measurements.md）

| 板子 | CPU 上限 | flash |
| --- | --- | --- |
| 幸狐 Pico 2（RP2350A） | 564 过、570 挂 | ≤57 MHz 稳、78.75 MHz 出错；上限未定位 |
| 官方 Pico 2（RP2350A） | 564 过、570 挂（已核实） | DIV 4（130 MHz）锁死、DIV 10（52 MHz）正常 |
| 合宙 RP2040 | 420 过、440 锁死 | 105 MHz（DIV 4）长期正常 |

两块 RP2350 板在**同一个地方**停住 ⇒ ≈565–570 MHz 是芯片边界；而 flash 上限明显不同
⇒ 那是**板子/flash 芯片**的事，不是 RP2350 接口的事。

## 待办

- 分频阶梯的中间点（官方板 DIV 6/8、幸狐板整套）。
- Pico W（RP2040 侧同一套：CPU 125/240/300/400/420 + 分频阶梯）。
- 运行时改 SSI 分频：现在分频按构建上限算，导致 520 MHz 的构建即使在 150 MHz 也把 flash
  压到 15 MHz（`SSI_BAUDR` 可以运行时写，只会更慢不会更快）。
- trace 上限 64 条：长爬升会被截断，可能让**生成的头文件漏掉最高档**（应当增量维护档位表）。
