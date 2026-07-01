# B1 讲义完成度与实现索引

本文按 B1 总线讲义逐项核对当前 RV32E NPC。这里的“完成”以实际 Verilator
运行路径为准，即 `npc/vsrc` + `npc/csrc` + `abstract-machine/am/src/riscv/npc`。

## 1. 完成度结论

| 讲义项目 | 状态 | 主要证据 |
| --- | --- | --- |
| 用消息和握手方式重构 NPC | 完成 | 各级 `valid/ready`，状态副作用由提交握手门控 |
| 评估总线改造前的单周期 NPC | 完成 | 基于提交 `66de2c2` 复测 MicroBench 和 STA |
| IFU/LSU 支持 SimpleBus | 完成并被 AXI4-Lite 取代 | Git 历史提交 `a23de15`、本文第 3 节 |
| 有效信号、完整握手、随机延迟 | 完成 | `NPC_AXI_MODE=random`、协议稳定性检查 |
| 总线错误传递和 Access Fault | 完成 | `DECERR` 转换为 `mcause=1/5/7` |
| IFU/LSU 改成 AXI4-Lite | 完成 | AR/R、AW/W/B 状态机 |
| AXI4-Lite 仲裁器 | 完成 | IFU/LSU 合并到单一 SRAM 接口 |
| Xbar 和地址译码 | 完成 | SRAM/UART/CLINT/DECERR 路由 |
| AXI4-Lite UART | 完成 | `0xa00003f8`，RTL 输出低 8 位字符 |
| AXI4-Lite CLINT | 完成 | 64 位 `mtime`，每周期加 1 |
| 修改 AM_TIMER_UPTIME | 完成 | high-low-high 读取和 375 MHz 换算 |
| 评估总线化 NPC 主频和程序性能 | 完成 | 375/380 MHz STA、MicroBench train |
| 随机延迟启动 RT-Thread | 完成 | seed 23 下执行预置命令并输出最终 `msh />` |

B1 中 APB、PMA/PMP、AXI 窄传输和完整 AXI4 是原理说明或后续 SoC 内容，不是本阶段
必须实现的模块。当前没有实现通用 PMA/PMP，也没有实现 AXI4 burst/ID。

## 2. 权威实现范围

实际构建由 [Makefile](../Makefile) 收集 `vsrc/*.v` 和 `csrc/*.cpp`，顶层模块为
[top.v](../vsrc/top.v)。因此：

- `vsrc` 是当前功能和 STA 的权威 RTL。
- `csrc` 是 Verilator 外部 SRAM、DiffTest、trace、sdb 和性能统计环境。
- `src/main/scala/npc` 保留早期 Chisel 级间消息设计，当前不生成 Verilator 使用的
  顶层，也尚未镜像最新仲裁器、Xbar 和 Access Fault。

Chisel 版本仍可用于理解 `Decoupled` 和级间消息，但不能用 `sbt run` 的结果替代
本文列出的 B1 验收。

## 3. 从 SimpleBus 到 AXI4-Lite

SimpleBus 的三个演进步骤已经在 Git 历史中完成：

1. 请求/响应有效信号：CPU 不再假设组合存储器或固定一周期响应。
2. `reqReady/respReady`：请求方和响应方均可反压。
3. AXI4-Lite：拆分为 AR、R、AW、W、B 五个独立通道。

最终代码不继续保留 SimpleBus 兼容端口，因为 AXI4-Lite 已覆盖它的请求、响应、
反压和错误信息语义。历史实现和设计变化记录在
[b1-stage-bus-refactor.md](b1-stage-bus-refactor.md)。

## 4. IFU

实现位置：[npc_if_stage.v](../vsrc/npc_if_stage.v)。

状态机：

```text
REQUEST --AR fire--> RESPONSE --R fire--> OUTPUT --out fire--> REQUEST
```

各状态职责：

- `REQUEST`：持续拉高 `arvalid`，保持 `araddr=pc`，直到 AR 握手。
- `RESPONSE`：无条件准备接收 R，将指令和 `rresp` 锁存。
- `OUTPUT`：通过级间 `valid/ready` 将锁存指令交给 IDU。

单独的 `OUTPUT` 缓冲很重要。若把 R 响应组合直通译码和 LSU，load 指令会形成
“IFU 等后级、LSU 等仲裁器、仲裁器等 IFU 释放”的循环等待。

当 `rresp != OKAY` 时，IFU 设置 instruction access fault。异常沿流水级到达 EXU，
保存：

```text
mepc   = faulting PC
mcause = 1
pc     = mtvec
```

## 5. LSU

实现位置：[npc_mem_stage.v](../vsrc/npc_mem_stage.v)。

Load 状态机：

```text
IDLE --AR fire--> READ_RESPONSE --R fire--> IDLE
```

Store 状态机：

```text
IDLE -> WRITE_REQUEST
WRITE_REQUEST --AW/W 分别握手--> WRITE_RESPONSE
WRITE_RESPONSE --B fire--> IDLE
```

AW 和 W 分别使用 `aw_done/w_done` 记录完成状态，因此允许任意握手先后顺序。store
只在收到 B 响应后提交，不会因为反压重复执行。

错误响应处理：

- load `rresp != OKAY`：禁止目的寄存器写回，`mcause=5`，跳转 `mtvec`。
- store `bresp != OKAY`：`mcause=7`，跳转 `mtvec`。

异常 CSR 更新发生在错误响应与写回级真正握手时，避免在下游反压期间重复产生副作用。

## 6. 仲裁器

实现位置：[npc_axi_arbiter.v](../vsrc/npc_axi_arbiter.v)。

读通道有两个 master：IFU 和 LSU。仲裁器完成三项职责：

- 调度：空闲时选择有效请求，当前策略为 LSU 优先。
- 阻塞：未获授权 master 的 `arready/rvalid` 保持无效。
- 转发：AR 握手时锁存 owner，直到 R 握手后释放。

写通道只有 LSU，因此 AW/W/B 直接转发。当前核心同一时刻最多有一个 LSU 事务，
不需要 AXI ID，也不支持乱序响应。

## 7. Xbar

实现位置：[npc_axi_xbar.v](../vsrc/npc_axi_xbar.v)。

地址译码：

| 地址 | 目标 | 权限 |
| --- | --- | --- |
| `0x80000000-0x87ffffff` | 外部 SRAM | 读写 |
| `0xa0000048` | CLINT `mtime[31:0]` | 只读 |
| `0xa000004c` | CLINT `mtime[63:32]` | 只读 |
| `0xa00003f8` | UART TX | 只写 |
| 其他 | error slave | 返回 `DECERR` |

Xbar 在请求握手时锁存目标设备，确保后续 R/B 返回给同一事务。写路径按
AW、W、B 顺序路由；master 仍可同时声明 AWVALID/WVALID，Xbar 会分别反压并依次
接收，不违反 AXI4-Lite 的独立通道规则。

## 8. UART

实现位置：[npc_axi_uart.v](../vsrc/npc_axi_uart.v)。

UART slave 包含 AW、W、B 三个通道：

1. 接收并记录 AW。
2. 接收 W；若 `wstrb[0]` 有效，通过 Verilator `$write()` 输出 `wdata[7:0]`。
3. 保持 BVALID，直到 master 拉高 BREADY。

输出位于 RTL，C++ 的 `pmem_write32()` 已删除 UART 地址特判。因此 UART 是否输出
完全取决于 AXI 请求是否正确到达设备。

## 9. CLINT

实现位置：[npc_axi_clint.v](../vsrc/npc_axi_clint.v)。

- `mtime` 为 64 位寄存器。
- reset 后从 0 开始，每个 NPC 时钟周期加 1。
- AR 握手时按地址选择高 32 位或低 32 位并锁存返回值。
- RVALID 保持到 RREADY。
- 写访问由 Xbar 返回 `DECERR`。

AM 实现在
[timer.c](../../../abstract-machine/am/src/riscv/npc/timer.c)，使用
high-low-high 顺序读取，防止低 32 位进位时组合出错误的 64 位时间。周期按
`NPC_CLOCK_FREQ=375000000` 换算为微秒。

## 10. Access Fault

实现分布：

- [npc_axi_xbar.v](../vsrc/npc_axi_xbar.v)：未映射或权限不匹配返回 `DECERR`。
- [npc_if_stage.v](../vsrc/npc_if_stage.v)：生成 instruction access fault。
- [npc_mem_stage.v](../vsrc/npc_mem_stage.v)：生成 load/store access fault。
- [npc_csr_file.v](../vsrc/npc_csr_file.v)：写入 `mepc/mcause`。
- [minirv_core.v](../vsrc/minirv_core.v)：门控 CSR 副作用并连接异常路径。

永久回归测试位于 [access-fault.S](../tests/access-fault.S)，依次触发并检查：

```text
instruction access fault: mcause = 1
load access fault:        mcause = 5
store access fault:       mcause = 7
```

运行：

```bash
make test-access-fault
```

## 11. 仿真 SRAM 和随机延迟

实现位置：

- [npc_step.cpp](../csrc/npc_step.cpp)：AXI slave 状态和时钟推进。
- [npc_memory.cpp](../csrc/npc_memory.cpp)：仅处理 PMEM 数据。

固定模式：

```bash
NPC_AXI_MODE=fixed
```

随机模式：

```bash
NPC_AXI_MODE=random NPC_AXI_SEED=7
```

随机模式会改变 AR/AW/W ready、R/B 响应延迟和写回 ready，并检查请求载荷在
`valid && !ready` 期间保持稳定。它覆盖握手、反压和响应保持，不依赖固定延迟。

## 12. 性能评估

### 12.1 总线改造前基线

以总线重构前提交 `66de2c2` 复测：

```text
MicroBench train:
cycles       = 196051276
instructions = 196051276
IPC          = 1.000000

Nangate45 STA:
critical path = 1.906 ns
reported Fmax = 509.554 MHz
500 MHz slack = +0.037 ns
510 MHz slack = -0.003 ns
area          = 10635.744 um^2
```

按 500 MHz 保守频率，估算运行时间为 `0.392 s`。

### 12.2 当前总线化 NPC

```text
MicroBench train:
cycles       = 625796246
instructions = 196058201
IPC          = 0.313294

Nangate45 STA:
critical path = 2.611 ns
reported Fmax = 376.892 MHz
375 MHz slack = +0.013 ns
380 MHz slack = -0.022 ns
area          = 13003.942 um^2
```

AM 实测总时间为 `1668.617 ms`。按 375 MHz 保守频率，估算运行时间为
`625796246 / 375000000 = 1.669 s`，两者相符。

当前实现总线化后并没有获得更高性能：IPC 下降到约 31.3%，同时组合逻辑增加使
Fmax 下降。这个结果说明后续需要流水化、缓存或缩短关键路径，不能只凭“多周期主频
通常更高”的经验推断当前实现。

STA 只包含 NPC、仲裁器、Xbar、UART 和 CLINT，不包含 C++ SRAM。它是综合后、
理想时钟和零布线延迟结果，不等价于布局布线签核频率。

## 13. 验证命令

```bash
cd ~/ysyx-workbench/npc
make lint
make all
make test-access-fault
make sta YOSYS=/path/to/yosys STA_FREQ_MHZ=375

cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
NPC_AXI_MODE=random NPC_AXI_SEED=20260621 \
  make ARCH=riscv32e-npc ALL="add load-store movsx" difftest

cd ~/Templates/rt-thread-am/bsp/abstract-machine
NPC_AXI_MODE=random NPC_AXI_SEED=23 \
  make ARCH=riscv32e-npc run-batch
```

RT-Thread 是常驻 shell，输出最终 `msh />` 后不会自动退出；自动验证时应使用
`timeout`，看到最终提示符即证明预置命令执行完成。
