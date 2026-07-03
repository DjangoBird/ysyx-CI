# PA 学习记录总文档

本文面向复盘和答辩，范围覆盖当前仓库已经完成并验证过的 D 阶段、C 阶段、B1，
以及 PA4 阶段 1 到 RT-Thread 启动为止的内容。后续 Nanos-lite 用户进程、Busybox、
虚拟内存等内容不在当前实现范围内。

本文不是简单索引。每一节都回答官网讲义中的实现任务和思考问题，并结合 git log
说明代码为什么这样改、这些改动在系统中承担什么意义、遇到问题时怎么 debug。

参考资料：

- 一生一芯 v24.07 D1-D6、C1-C5、B1 讲义：
  `https://ysyx.oscc.cc/docs/2407/d/1.html` 到
  `https://ysyx.oscc.cc/docs/2407/b/1.html`
- PA4.1 多道程序讲义：
  `https://ysyx.oscc.cc/docs/ics-pa/4.1.html`
- 当前仓库历史：
  `git log --oneline --decorate -- nemu abstract-machine npc`

## 1. 当前实现和文档地图

当前文档分工：

| 文档 | 用途 |
| --- | --- |
| `c-d-stage-experiment-report.md` | D/C/B1 阶段实验报告，按阶段说明实现、运行、debug |
| `stage-notes/*.md` | 每个阶段的拆分学习记录 |
| `trace.md` | NEMU itrace/mtrace/ftrace/etrace 机制说明 |
| `pa4-stage1-rv32.md` | RV32 NEMU PA4.1 上下文切换和 RT-Thread 启动记录 |
| `c5-npc-rtthread.md` | NPC 启动 RT-Thread 的补充记录 |
| `pa-learning-record.md` | 本文，总结官网问题、git 历史、debug 方法和工具使用 |

当前代码范围：

- NEMU：RV32IM、CSR/异常、trace、DiffTest、设备 IO。
- Abstract Machine：`riscv32-nemu`、`riscv32e-npc`，TRM/IOE/CTE。
- NPC：RV32E、多周期/握手、AXI4-Lite、UART/CLINT、sdb/trace/DiffTest。
- RT-Thread：能在 AM BSP 上启动到 `msh />`。

## 2. Git 历史对应的模块演进

这部分按提交说明“改了什么”和“为什么有意义”。查看细节：

```bash
git show --stat <commit>
git show <commit> -- <path>
```

### 2.1 NEMU 指令和 AM 基础

| 提交 | 主要文件 | 修改意义 |
| --- | --- | --- |
| `c4e4e3c` | `nemu/src/isa/riscv32/inst.c` | 添加 RV32 基础指令，是 NEMU 能跑 AM/cpu-tests 的前提 |
| `0ef557e` | `abstract-machine/scripts/platform/nemu.mk` | 增加 batch 运行，自动测试不再停在 `(nemu)` |
| `defc96b` | `nemu/Kconfig`、`cpu-exec.c`、`paddr.c`、`ftrace.c` | 建立 trace 基础设施，能观察指令、函数和内存行为 |
| `2c16d95` | `difftest/dut.c`、`cpu-exec.c`、`sdb.c` | 完善 DiffTest 和 sdb，能把 DUT 与 REF 状态逐指令对齐 |
| `e81c6b2` | `am/src/platform/nemu/ioe/*`、`nemu/src/device/io/map.c` | 接入 timer/input 等设备 IO，AM 的 `am-tests` 可测试外设 |

这些提交体现了 PA 的基本路径：先让机器能执行指令，再让程序能批量运行，然后让
调试工具能回答“错在哪里”。

### 2.2 NPC 从 minirv 到 RV32E

| 提交 | 主要文件 | 修改意义 |
| --- | --- | --- |
| `927751b` | `npc/vsrc/minirv_core.v`、`top.v` | 早期 minirv 处理器，证明 RTL 能执行简单指令 |
| `6e295a8` | `npc/csrc/main.cpp`、`platform/npc.mk`、`trm.c` | AM-NPC 打通，NPC 不再只能跑手写小程序 |
| `55d3ac1` | `npc/csrc/*`、`npc/vsrc/npc_*_stage.v` | 拆出 C++ runtime 和 IF/ID/EX/MEM/WB，形成可维护结构 |
| `d9a43f4` | `npc/vsrc/npc_ex_stage.v`、`npc/README.md` | 向 RV32E 扩展，明确 `x0-x15` 的 ISA 边界 |

代码意义：

- `main.cpp` 只负责仿真入口，不应该混入 CPU 语义。
- `npc_memory.cpp` 只模拟外部 SRAM。
- `npc_step.cpp` 负责推进时钟和外部总线。
- `npc_*_stage.v` 才是 CPU 架构状态变化的地方。

这种拆分让后续加入 DiffTest、trace、AXI-Lite 时不需要把所有代码重写。

### 2.3 PA4.1、异常和 RT-Thread

| 提交 | 主要文件 | 修改意义 |
| --- | --- | --- |
| `e76b36c` | `riscv.h`、`cte.c`、`trap.S`、`inst.c`、`intr.c` | 实现 RV32 上下文切换所需 CSR、`ecall/mret`、Context |
| `777ed00` | `nemu/Kconfig`、`riscv32/system/intr.c` | 增加 etrace，异常入口可观测 |
| `5d8e778` | `nemu/src/isa/riscv32/inst.c` | 支持 `mvendorid/marchid`，给系统软件识别平台 |
| `66de2c2` | `npc_csr_file.v`、`npc_ex_stage.v`、`npc_memory.cpp`、AM NPC CTE | NPC 支持 machine-mode CSR 和 RT-Thread 启动 |

代码意义：

- `ecall` 不只是“跳转”，它必须保存 `mepc/mcause`。
- `mret` 不只是普通 `jalr`，它从 CSR 中恢复异常返回地址。
- `trap.S` 的 `mv sp, a0` 是上下文切换的本质：从旧 Context 切换到新 Context。
- etrace 放在 NEMU 内部，不影响客户程序行为，适合定位“还没进入 CTE 就错”的问题。

### 2.4 B1 总线化

| 提交 | 主要文件 | 修改意义 |
| --- | --- | --- |
| `a23de15` | `npc/src/main/scala/*`、`npc/vsrc/*` | 建立 valid/ready 和 SimpleBus 思想，副作用由握手门控 |
| `65ababd` | `npc/csrc/npc_step.cpp`、`npc_difftest.cpp` | C++ runtime 支持可变延迟总线，DiffTest 只在提交点推进 |
| `ede3849` | `npc/docs/b1-stage-bus-refactor.md` | 记录 SimpleBus 协议和验证方法 |

后续工作树中还继续加入：

- `npc_axi_arbiter.v`：IFU/LSU 读仲裁。
- `npc_axi_xbar.v`：SRAM/UART/CLINT 地址译码。
- `npc_axi_uart.v`：RTL 串口输出。
- `npc_axi_clint.v`：RTL `mtime`。
- `npc_mem_stage.v`：load/store AXI 状态机和 access fault。

### 2.5 按模块看修改历史

| 模块 | 相关提交 | 代码意义 | 调试入口 |
| --- | --- | --- | --- |
| NEMU decode/execute | `c4e4e3c`、`e76b36c`、`5d8e778` | 从基础 RV32 指令扩展到 CSR、`ecall/mret`、ID CSR，支撑 AM 和 RT-Thread | `inst.c`、`intr.c`、`isa-def.h` |
| NEMU trace | `defc96b`、`777ed00` | 从指令/函数/内存 trace 扩展到异常 trace，让控制流、访存、异常都可观测 | `cpu-exec.c`、`ftrace.c`、`paddr.c`、`intr.c` |
| NEMU DiffTest/sdb | `2c16d95` | 建立 REF 对比和交互调试，定位“哪条指令后状态不同” | `difftest/dut.c`、`sdb.c` |
| NEMU 设备 | `e81c6b2` | MMIO map、timer/input 等 IOE 接入，AM 设备测试可运行 | `device/io/map.c`、AM `ioe/*` |
| AM NEMU CTE | `e76b36c` | Context 布局、`kcontext()`、`trap.S` 切换 `sp`，实现 PA4.1 上下文切换 | `riscv.h`、`cte.c`、`trap.S` |
| AM NPC 平台 | `6e295a8`、`66de2c2` | NPC 也能通过 AM 编译、加载、输出、计时、异常返回 | `platform/npc.mk`、`riscv/npc/*.c` |
| NPC C++ runtime | `6e295a8`、`55d3ac1`、`65ababd` | 从单个 `main.cpp` 演进为 runtime/memory/step/sdb/trace/difftest 分层 | `npc/csrc/*.cpp` |
| NPC RV32E RTL | `927751b`、`55d3ac1`、`d9a43f4` | 从 minirv 到 RV32E，拆分 IF/ID/EX/MEM/WB，限制 `x0-x15` | `npc/vsrc/npc_*_stage.v` |
| NPC CSR/RT-Thread | `66de2c2` | 增加 CSR 文件、`ecall/mret`、ID CSR、timer，启动 RT-Thread | `npc_csr_file.v`、`npc_ex_stage.v` |
| NPC bus/AXI | `a23de15`、`65ababd`、后续工作树 | valid/ready、SimpleBus、AXI-Lite、Arbiter、Xbar、UART、CLINT、Access Fault | `npc_axi_*.v`、`npc_step.cpp` |

这张表的用法：如果某个测试失败，先按现象选择模块，再用相关提交查看设计演进。例如
`load-store` 错误通常看 NEMU/NPC decode 和 MEMU；RT-Thread shell 少字符通常看
NPC bus/AXI 和 UART；yield 卡死通常看 AM CTE 和 CSR。

## 3. 官网问题和逐题回答

本节按阶段回答官网讲义中的问题和实现任务。由于部分 D/C 阶段页面明确写着“讲义内容未就绪”，
其问题主要来自对应 PA 内容和当前实现要求。

### 3.1 D1：为什么先实现 RV32IM NEMU？

任务：根据 PA2 阶段 1 实现支持 RV32IM 的 NEMU。

回答：NEMU 是后续所有系统软件的参考机器。先实现 RV32IM 有三个意义：

1. 用 C 写出的解释器能帮助理解 ISA 语义。
2. 后续 NPC DiffTest 需要一个可信 REF。
3. AM、RT-Thread、Nanos-lite 等软件都依赖稳定的基础指令和异常行为。

当前实现位置：

- `nemu/src/isa/riscv32/inst.c`
- `nemu/src/isa/riscv32/system/intr.c`
- `nemu/src/isa/riscv32/include/isa-def.h`

关键理解：

- `isa_exec_once()` 取指并调用译码执行。
- `decode_exec()` 根据模式串匹配指令。
- 普通指令修改 `dnpc = snpc`，跳转/异常修改 `dnpc` 到目标地址。

### 3.2 D2：`.elf`、`.bin`、`.txt` 分别有什么用？

回答：

- `.elf` 保留段、符号、入口等信息，给 ftrace、readelf、objdump 使用。
- `.bin` 是裸镜像，加载到模拟器物理内存执行。
- `.txt` 是反汇编文本，适合和 itrace 对照。

调试时如果 `pc=0x800001d8`，第一步不是猜寄存器，而是打开：

```bash
less build/<program>-riscv32-nemu.txt
rg "800001d8" build/<program>-riscv32-nemu.txt
```

再看它对应的是正常路径、assert 路径，还是 bad trap 路径。

### 3.3 D3：AM 为什么要存在？

任务：完成 PA2 中“程序、运行时环境与 AM”。

回答：AM 是应用和具体机器之间的薄抽象层。它把启动、堆、输出、设备、异常等接口统一成
TRM/IOE/CTE，应用不需要直接知道 NEMU/NPC 的 MMIO 细节。

当前链路：

```text
_start
  -> _trm_init()
  -> main(mainargs)
  -> halt(ret)
  -> ebreak
  -> NEMU/NPC trap
```

为什么链接地址是 `0x80000000`？

因为 NEMU/NPC 的 PMEM 起点就是 `0x80000000`。如果 ELF 链接地址和加载地址不一致，
PC 相对寻址、全局变量地址、栈区都会错。

### 3.4 D4：为什么要模块化 RTL？

讲义要求把 minirv/NPC 划分为 IFU、IDU、EXU、LSU、WBU。

回答：模块化不是为了文件数量好看，而是为了让每级职责清晰：

- IFU：只处理 PC 和取指。
- IDU：只解析字段和立即数。
- EXU：做 ALU、跳转、CSR、访存请求生成。
- LSU/MEMU：处理数据访存响应。
- WBU：更新 PC 和 GPR。

这样当 load 出错时，不需要全局乱查：

1. 地址算错，看 EXU。
2. 数据扩展错，看 MEMU。
3. 寄存器没写，看 WBU。
4. 指令没进来，看 IFU。

讲义问题：模块之间接口怎么梳理？

回答：接口应该按“阶段消息”来梳理，而不是按临时信号堆砌。当前 B1 后的接口是
`valid/ready + payload` 的思路：上游给出消息，下游 ready 后才产生状态副作用。

### 3.5 D5：为什么设备使用 MMIO？

任务：完成设备和输入输出。

回答：MMIO 把设备寄存器映射到物理地址空间。CPU 不需要新增专门的 IO 指令，只要对
特定地址执行 load/store，就能访问设备。

NEMU 中：

```text
paddr_read/write
  -> in_pmem(addr) ? pmem : mmio
```

NPC B1 中：

```text
LSU AXI
  -> Xbar decode address
  -> SRAM/UART/CLINT/error
```

为什么 timer 要读高低 32 位？

CLINT `mtime` 是 64 位，RV32 一次只能读 32 位。正确做法是 high-low-high：

```text
hi1 = mtime_hi
lo  = mtime_lo
hi2 = mtime_hi
if hi1 != hi2 retry
```

这样避免低 32 位在读取过程中溢出导致拼接出错误时间。

### 3.6 D6/B1：为什么先实现总线？

官网 B1 问题：“为什么要先实现总线？”

回答：总线化是把 NPC 从“能在仿真里跑”推进到“接近可流片系统”的关键。单周期处理器
直接调用 C++ `pmem_read()` 是仿真技巧，真实硬件不能这样做。总线提供：

- 可变延迟。
- 请求和响应解耦。
- 多 master 仲裁。
- 多设备地址译码。
- 错误响应。

所以先实现总线不是为了追求性能，而是为了让系统边界真实。性能优化在后续流水线、
缓存和时序优化中再量化处理。

### 3.7 B1：valid/ready 要遵守什么？

回答：

1. `valid` 表示发送方有有效请求或响应。
2. `ready` 表示接收方本周期能接收。
3. `fire = valid && ready` 才是事务发生。
4. `valid && !ready` 时 payload 必须保持稳定。
5. 不能让 master 和 slave 都等对方先拉高信号，否则会死锁。

当前 C++ runtime 中 `check_stability()` 专门检查第 4 点。随机反压能暴露固定 ready 下
看不出来的协议 bug。

### 3.8 B1：AXI 为什么分 AR/R/AW/W/B 五个通道？

回答：

- AR/R 分离：读地址和读数据可以不同周期完成。
- AW/W 分离：写地址和写数据可以独立到达。
- B 通道：写操作有完成响应，store 不能在 AW/W 之后就立即提交。

这也是 RT-Thread 最后 `msh />` 缺失问题的根因之一：如果 UART store 没有等到 B
响应就提交，或者 AW/W 反压下丢了字符，shell 输出就会少。

### 3.9 B1：为什么需要仲裁器？

回答：IFU 和 LSU 都可能访问内存，但真实 SRAM 通常只有一个接口。仲裁器负责：

- 选择一个 master。
- 阻塞其它 master。
- 记录读响应应该返回给谁。

当前实现选择 LSU 优先。因为 NPC 是简单多周期结构，同时未完成事务很少，暂时不需要复杂公平调度。

### 3.10 B1：为什么需要 Xbar？

回答：Xbar 是硬件中的 MMIO 地址译码器。它把同一条 AXI 请求按地址发给不同设备：

- `0x80000000-0x87ffffff`：SRAM。
- `0xa00003f8`：UART。
- `0xa0000048/0xa000004c`：CLINT。
- 其它地址：error slave，返回 `DECERR`。

没有 Xbar 时，所有 MMIO 都只能靠 C++ 仿真环境特判，不接近真实硬件。

### 3.11 B1：PMA/PMP 和 Access Fault 怎么理解？

讲义指出设备地址不一定可执行、可读、可写。

回答：PMA/PMP 是权限检查机制。当前 NPC 没有完整 PMA/PMP，但用 Xbar 的错误响应实现了
基础 access fault：

- 取指错误：`mcause=1`。
- load 错误：`mcause=5`。
- store 错误：`mcause=7`。

这能防止程序把 UART 返回值当指令继续执行，也能让软件知道访问未映射地址失败。

### 3.12 PA4.1：为什么不同进程需要不同栈？

回答：栈保存函数调用帧、局部变量、返回地址和异常 Context。如果两个执行流共享同一个栈：

- A 的函数调用可能覆盖 B 的返回地址。
- A 触发 trap 保存的 Context 可能覆盖 B 的 Context。
- 调度器无法可靠地恢复任何一个执行流。

所以每个线程/进程必须有独立栈，PCB 中保存该执行流当前 Context 指针。

### 3.13 PA4.1：上下文切换本质是什么？

回答：不是复制一堆寄存器到全局变量，而是切换 `sp` 指向的 Context：

```text
A ecall
  -> trap.S 把 A 寄存器保存到 A 栈
  -> __am_irq_handle 返回 B 的 Context 指针
  -> trap.S: mv sp, a0
  -> 从 B 栈恢复寄存器
  -> mret 后执行 B
```

所以 `trap.S` 中的 `mv sp, a0` 是上下文切换最关键的一行。

### 3.14 PA4.1：刚创建的线程如何第一次运行？

回答：`kcontext()` 在线程栈顶人工构造一个“看起来像 trap 保存出来”的 Context：

- `mepc = entry`。
- `mstatus = 0x1800`。
- `a0 = arg`。
- 其它寄存器清零。

调度到这个 Context 后，`trap.S` 会像恢复普通异常现场一样恢复它，最后 `mret` 到
`entry`，线程就开始运行。

### 3.15 PA4.1：为什么 `mepc += 4`？

回答：RISC-V `ecall` 是 4 字节指令。异常发生时 `mepc` 保存的是 `ecall` 自己的地址。
如果不加 4，`mret` 后会再次执行同一条 `ecall`，表现为 yield 死循环。

### 3.16 PA4.1：为什么 RV32E 的 yield 用 `a5`？

回答：RV32I 通常用 `a7/x17` 传 syscall 编号，但 RV32E 只有 `x0-x15`，没有 `x17`。
因此 `riscv.h` 中：

```c
#ifdef __riscv_e
#define GPR1 gpr[15] // a5
#else
#define GPR1 gpr[17] // a7
#endif
```

AM 的 `yield()` 也必须按 ISA 分支，否则 CTE 会把 yield 误判成普通 syscall。

### 3.17 PA4.1：为什么叫内核线程而不是内核进程？

回答：在当前 PA 阶段，它们都可以视为“执行流”。严格来说，进程通常带独立地址空间和资源，
线程共享进程资源。当前没有虚拟内存和用户态隔离，因此“内核线程”更准确：它们共享同一
内核地址空间，只是拥有各自栈和 Context。

### 3.18 PA4.1：机制和策略如何解耦？

回答：

- 机制：AM CTE 提供保存/恢复 Context、`kcontext()`、`yield()`。
- 策略：OS/RT-Thread 决定下一个运行哪个线程。

这样 AM 不关心调度算法，RT-Thread 不需要自己写底层 trap 保存恢复代码。

### 3.19 B1：同时读写同一个地址会发生什么？

讲义要求 RTFM。结论是：读写同地址的返回值不是由 AXI 协议统一规定的，而由具体
slave 的存储器语义规定。有些 SRAM/Block RAM 会返回旧值，有些会返回新值，有些
行为未定义。

当前 NPC 可以不专门处理这个问题，因为：

- IFU 只发读请求。
- LSU 对同一条指令要么 load，要么 store，不会同一时刻自己同时读写。
- 简单多周期核心没有多个未完成 LSU 事务。

如果未来做流水线或 cache，就不能继续依赖这个简化假设，需要在 LSU/cache 中定义
读写冲突策略。

### 3.20 B1：如何避免握手死锁和活锁？

死锁例子：master 等 ready 才拉 valid，slave 等 valid 才拉 ready，双方一直等。

活锁例子：master 和 slave 轮流试探，但每次都错开，永远没有 `valid && ready`。

当前实现的原则：

- IFU/MEMU 在有请求时主动保持 `valid`，直到握手。
- C++ SRAM slave 可以随机拉低 `ready`，但不会要求 master 先撤销 `valid`。
- `valid && !ready` 时地址、写数据、写掩码保持稳定。
- R/B 响应 `valid` 保持到 CPU 拉高 `ready`。

这也是 `NPC_AXI_MODE=random` 的价值：它会破坏“每次都同拍握手”的侥幸路径。

### 3.21 B1：AXI4-Lite 相比 SimpleBus 多出来的价值是什么？

SimpleBus 能表达请求/响应和反压，但 AXI4-Lite 更接近真实 SoC 接口：

- 读写通道分离。
- 写地址、写数据、写响应分离。
- 每个通道都有独立握手。
- 响应带 `resp`，可以表达错误。
- 可以自然接入 Arbiter、Xbar 和多个设备。

当前 NPC 没有利用 AXI 的并发性能优势，但先把接口换成 AXI4-Lite，可以降低后续
接 SoC 时的结构差异。

### 3.22 B1：UART 为什么放到 RTL 而不是 C++ 特判？

回答：C++ 特判 MMIO 是仿真环境技巧，不是硬件结构。B1 的目标是让外设也像真实硬件
一样挂在总线上。RTL UART slave 的意义是：

- CPU 通过 AXI store 访问 UART。
- Xbar 按地址选择 UART。
- UART 自己完成 AW/W/B 握手。
- `$write()` 只作为仿真输出字符的后端。

因此 UART 的可见行为由总线事务决定，不再由 `pmem_write32()` 中的 if 判断决定。

### 3.23 B1：CLINT 的 `mtime` 为什么要 64 位？

回答：如果只用 32 位，在较高频率下很快溢出。例如 500 MHz 下，`2^32` 周期约
8.59 秒。64 位可以让时间计数在实际运行中足够长。

当前 NPC 是 32 位处理器，因此软件要读两次 32 位并拼成 64 位。AM 的 high-low-high
读取方式避免了读取过程中低位溢出造成的撕裂。

### 3.24 B1：Access Fault 为什么要在响应点产生？

回答：总线错误不是请求发出时就一定知道。对于 load，错误体现在 R 响应；对于 store，
错误体现在 B 响应。因此 access fault 必须在响应握手时产生。

如果在请求点提前提交，会出现两个问题：

- load 可能把错误数据写入寄存器。
- store 可能在还不知道写是否成功时就改变 PC 和架构状态。

当前 MEMU 在 R/B 响应点设置 `mcause=5/7`，并禁止 load 写回。

### 3.25 C1：为什么 trace、watchpoint、DiffTest 都放在指令后？

回答：这些工具观察的是“架构状态”。一条指令执行中间可能有临时变量、访存请求、
组合信号，不能代表最终状态。指令执行完成后：

- PC 已确定。
- GPR/CSR/内存副作用已完成或被判定异常。
- DiffTest REF 可以同步执行一条。
- watchpoint 才能判断表达式是否真的改变。

NPC 多周期后，这个边界进一步变成 `commit_valid`，不是任意时钟周期。

### 3.26 C2：RV32E 和 RV32I 的主要差异是什么？

回答：RV32E 只有 16 个通用寄存器 `x0-x15`，ABI 也变成 `ilp32e`。因此：

- 硬件访问 `x16-x31` 应视作非法。
- 编译必须使用 `-march=rv32e_zicsr -mabi=ilp32e`。
- `a7/x17` 不存在，yield/syscall 标记要改用 `a5/x15`。
- DiffTest 只比较 16 个 GPR。

### 3.27 C2：为什么 RV32E 没有乘除也能跑 C 程序？

回答：编译器会把某些运算降级为 libgcc 运行时函数。例如除法、64 位乘法、移位等
可以由软件函数完成。当前 `riscv32e-npc.mk` 把 `div.S/muldi3.S/multi3.c/ashldi3.c`
加入 AM 源码，就是为了补齐这些运行时能力。

### 3.28 C4：为什么 ftrace 需要 ELF 而不是 bin？

回答：`.bin` 只有原始机器码，没有符号表；`.elf` 保留函数符号、地址、大小等信息。
ftrace 需要从 ELF 中找出函数符号，才能把 `jal/jalr/ret` 翻译成“调用哪个函数、从哪个
函数返回”。

所以 AM 运行脚本会同时生成 `.bin` 和 `.elf`：

- `.bin` 给模拟器加载。
- `.elf` 给 ftrace、readelf、objdump 使用。

### 3.29 PA4.1：`kcontext()` 要构造什么状态？

讲义提示“从 `__am_asm_trap()` 返回之后开始执行 `f()`”。因此 `kcontext()` 构造的
状态必须满足：

- `mepc` 指向线程入口。
- `mstatus` 能让 `mret` 返回到正确特权级。RV32 当前使用 `0x1800`。
- 栈指针对齐，Context 放在栈顶。
- 参数按 ABI 放到第一个参数寄存器，RV32/RV32E 都是 `a0/x10`。
- Context 结构和 `trap.S` 保存布局一致。

如果这些条件任一不满足，第一次调度到新线程时就会跳飞、参数错误或 trap 死循环。

### 3.30 PA4.1：`kcontext()` 为什么最好只写一个 Context？

回答：这样 OS 能预测 `kcontext(kstack, ...)` 的返回值就在 `kstack.end - sizeof(Context)`
附近，不会有额外隐藏栈帧或副作用。对 RISC-V 来说参数走寄存器，因此可以直接设置
`a0`，不需要像 x86 那样额外在栈上布置参数。

### 3.31 PA4.1：函数参数如何传给内核线程？

回答：按 ABI。RISC-V 第一个参数使用 `a0/x10`，所以：

```c
c->gpr[10] = (uintptr_t)arg;
```

线程第一次被恢复后从 `entry` 开始运行，此时硬件寄存器 `x10` 已经恢复成 `arg`，
C 函数自然能在形参中读到它。

### 3.32 PA4.1：RT-Thread BSP 和 AM 如何对应？

RT-Thread BSP 需要板级 API，AM 提供类似功能：

| RT-Thread 需求 | AM/NPC 对应 |
| --- | --- |
| 堆区 | TRM `heap` |
| 串口输出 | TRM `putch()` -> UART MMIO |
| 中断开关 | CTE `iset()`，当前简化 |
| 线程栈初始化 | AM `kcontext()` 思想或等价 Context 构造 |
| 上下文切换 | `yield/ecall/trap.S/mret` |
| 时间 | IOE timer 或 CLINT `mtime` |

因此启动 RT-Thread 不是单独修一个应用，而是验证 TRM、CTE、CSR、UART、timer 和
总线是否串起来。

## 4. Debug 过程和方法

### 4.1 分层定位原则

遇到 bug 时先判断在哪一层：

| 现象 | 优先看 |
| --- | --- |
| PC 跑飞 | `.txt` 反汇编、itrace、跳转指令 |
| 寄存器值错 | DiffTest、sdb `info r`、指令语义 |
| load/store 错 | mtrace、MEMU load 扩展、地址和掩码 |
| 异常没进入 | etrace、`mtvec/mepc/mcause` |
| 上下文不切换 | `trap.S`、`Context` 布局、`mepc += 4` |
| UART 少字符 | AXI AW/W/B、UART BVALID、store 提交点 |
| NPC 卡死无提交 | `NPC_AXI_DEBUG`、stall 计数、波形 |

更详细的分阶段排查流程见 [C3 调试技巧](stage-notes/c3-debug.md)，总线专项说明见
[B1 总线文档](stage-notes/b1-bus-axi.md)。这里保留常用命令和判断原则。

### 4.2 NEMU 中用 gdb 调试

构建带调试信息的 NEMU：

```bash
cd ~/ysyx-workbench/nemu
make ISA=riscv32 clean
make ISA=riscv32 CFLAGS_BUILD='-O0 -g'
```

如果 Makefile 不接受覆盖变量，可以直接在当前构建产物上使用 gdb，至少仍能看符号：

```bash
gdb --args build/riscv32-nemu-interpreter \
  -l /tmp/nemu-log.txt \
  ../am-kernels/tests/cpu-tests/build/load-store-riscv32-nemu.bin
```

常用断点：

```gdb
b decode_exec
b isa_exec_once
b paddr_read
b paddr_write
b isa_raise_intr
b nemu/src/isa/riscv32/inst.c:行号
run
```

常用查看：

```gdb
p/x cpu.pc
p/x cpu.gpr[10]
p/x s->pc
p/x s->isa.inst.val
bt
n
s
finish
```

定位 load 指令时：

1. 在 `.txt` 反汇编中找到出错 PC。
2. 在 `decode_exec` 或对应 `INSTPAT` 断点停住。
3. 查看 `src1 + imm`、`rd`、读内存长度、返回值。
4. 单步到写回寄存器后看 `cpu.gpr[rd]`。

注意：bad trap 时看到的 `a0` 不一定是出错 load 直接写出来的值。测试框架进入
fail 路径后可能执行 `auipc a0, 0` 或其它报错代码，导致 `a0` 变成某个 PC 附近地址。
所以要在出错指令刚执行完的边界看寄存器。

### 4.3 NEMU trace 配合 gdb

推荐组合：

```bash
make ARCH=riscv32-nemu ALL=load-store run-batch
less build/nemu-log.txt
less build/load-store-riscv32-nemu.txt
```

trace 用来快速缩小范围，gdb 用来验证具体表达式：

- itrace：确认哪条指令后状态变坏。
- mtrace：确认访存地址、长度、数据。
- ftrace：确认是否进入了错误函数路径。
- etrace：确认异常原因和入口。

### 4.4 NPC 中用 gdb 调试 Verilator C++ 外壳

构建：

```bash
cd ~/ysyx-workbench/npc
make clean
make all CXXFLAGS='-O0 -g'
```

运行：

```bash
gdb --args build/top -b path/to/program.bin
```

常用断点：

```gdb
b main
b single_cycle
b run_simulation
b npc_difftest_step
b npc_difftest_skip_ref
b npc_trace_commit
b pmem_read32
b pmem_write32
run
```

常用查看：

```gdb
p/x dut.dbg_pc_o
p/x dut.dbg_x10_o
p/x dut.commit_pc
p/x dut.commit_instr
p dut.commit_valid
p dut.mem_axi_arvalid
p dut.mem_axi_arready
p dut.mem_axi_awvalid
p dut.mem_axi_wvalid
p dut.mem_axi_bvalid
```

原则：

- C++ gdb 看的是 Verilator 顶层端口和 runtime 状态。
- RTL 内部组合细节更适合看波形。
- 只有 `commit_valid` 时的寄存器/PC 才是架构提交状态。

### 4.5 Verilator 波形调试

如果需要看 RTL 内部信号，打开 Verilator trace 或在 Makefile 中加入对应 `--trace`
配置，然后运行生成 `.vcd`。观察顺序：

1. `clk/rst`。
2. `commit_valid/commit_pc/commit_instr`。
3. IFU `arvalid/arready/rvalid/rready`。
4. MEMU `awvalid/awready/wvalid/wready/bvalid/bready`。
5. `pc_next/wb_en/wb_idx/wb_data`。
6. CSR `mepc/mcause/mtvec`。

不要一开始就看所有信号。先用现象判断模块，再加信号。

### 4.6 总线问题的最小判断法

总线调试时只认一个事实：

```text
fire = valid && ready
```

没有 fire，就没有事务发生。不要因为 `valid=1` 就认为请求已经被接收，也不要因为
`ready=1` 就认为数据已经被传输。

读请求最小检查：

```text
AR fire 了吗？
ARADDR 在等待 ready 时稳定吗？
R valid 回来了吗？
RRESP 是 OKAY 还是错误？
R fire 了吗？
load/IFU 是否只在 R fire 后继续？
```

写请求最小检查：

```text
AW fire 了吗？
W fire 了吗？
AW 和 W 是否可以不同周期？
BRESP 回来了吗？
B fire 了吗？
store 是否只在 B fire 后提交？
```

如果 fixed 通过、random 失败，优先怀疑以上任一规则被破坏。

### 4.7 典型 debug 复盘

#### `etrace` 不输出

排查：

1. `CONFIG_TRACE` 是否开。
2. `CONFIG_ETRACE` 是否开。
3. `TRACE_START/TRACE_END` 是否覆盖异常发生时刻。
4. 程序是否真的执行了 `ecall` 或 access fault，而不是普通 `ebreak` 结束。

修正思路：把 etrace 放在 `isa_raise_intr()` 中，而不是 AM CTE 的 `printf()` 中。这样即使
程序没进入异常处理函数，NEMU 也能记录异常。

#### RT-Thread 最后 `msh />` 不输出

排查：

1. 确认 shell 已执行到输出提示符前。
2. 看 UART store 是否发出 AW 和 W。
3. 看 UART 是否产生并保持 BVALID。
4. 看 MEMU 是否等 B 握手后才提交 store。
5. 看 stdout 是否 flush。

最终修正方向：UART 迁移到 RTL AXI-Lite slave，AW/W/B 完整握手，`$write` 后
`$fflush()`。

#### NPC load 后死锁

现象：外部 SRAM `RVALID=1`，但 `RREADY=0`，程序长期没有提交。

原因：IFU 把 R 响应组合直通后级，后级译码出 load 后 LSU 又向仲裁器申请总线，
形成 “IFU 等后级 ready，LSU 等 IFU 释放仲裁器” 的循环等待。

修正：IFU 增加 `OUTPUT` 状态，先锁存 R 响应并释放仲裁器，再通过级间握手给后级。

## 5. 运行和验证命令

NEMU cpu-tests：

```bash
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
make ARCH=riscv32-nemu ALL="dummy add load-store div" run-batch
```

NEMU yield-os：

```bash
cd ~/ysyx-workbench/am-kernels/kernels/yield-os
make ARCH=riscv32-nemu run-batch
```

NPC cpu-tests：

```bash
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
make ARCH=riscv32e-npc ALL="dummy add bit shift if-else load-store movsx" run-batch
```

NPC DiffTest：

```bash
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
NPC_AXI_MODE=random NPC_AXI_SEED=20260621 \
  make ARCH=riscv32e-npc ALL="add load-store movsx" difftest
```

NPC access fault：

```bash
cd ~/ysyx-workbench/npc
make test-access-fault
```

RT-Thread：

```bash
cd ~/Templates/rt-thread-am/bsp/abstract-machine
NPC_AXI_MODE=random NPC_AXI_SEED=23 \
  make ARCH=riscv32e-npc run-batch
```

看到最终 `msh />` 后，说明预置命令执行完成；RT-Thread 等待输入不会自动退出。
