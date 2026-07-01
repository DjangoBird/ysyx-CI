# B1：NPC 级间总线重构

本文记录 B1 讲义“重构 NPC”、SimpleBus 过渡实现和最终 AXI4-Lite 接口。
范围包含处理器内部级间消息通信、IFU/LSU 五通道握手、AXI 仲裁器、地址 Xbar、
UART、CLINT、随机反压测试、多周期 DiffTest 和主频/性能评估。

逐条完成度和“功能在哪个文件实现”的索引见
[b1-completion-checklist.md](b1-completion-checklist.md)。

## 1. 重构目标

原实现虽然已经拆成多个模块，但模块之间只是大量离散组合信号：

```text
IFU -> instruction -> IDU -> decode fields -> EXU -> writeback fields -> WBU
```

这种连接默认消息每周期有效、下游永远能够接收，接口本身无法表达等待。接入存在
延迟的存储器或改成多周期、流水线结构时，需要重新设计全局控制。

重构后的级间接口包含：

- `bits`：本级向下一级发送的消息载荷。
- `valid`：上游声明当前消息有效。
- `ready`：下游声明当前可以接收消息。
- `fire = valid && ready`：双方确认本周期完成一次消息传递。

完成第一步重构时仍保持单周期行为。SimpleBus 用于先建立请求/响应和反压语义，
随后 IFU 和 LSU 的外部访存接口替换为 AXI4-Lite。后续模块的级间接口和提交语义
无需重新设计。

## 2. Chisel 消息接口

Chisel 使用 `Decoupled` 表达握手总线，并为不同阶段定义消息 Bundle：

| 消息 | 主要内容 |
| --- | --- |
| `FetchMessage` | `pc`、指令、顺序下一 PC |
| `DecodeMessage` | 译码字段、立即数、寄存器值、PC |
| `ExecuteOut` | 写回控制、访存请求、跳转、异常、CSR 操作 |
| `WritebackMessage` | 最终写回数据、下一 PC、trap |

连接统一经过：

```scala
object StageConnect {
  def apply[T <: Data](left: DecoupledIO[T], right: DecoupledIO[T]): Unit = {
    right.bits := left.bits
    right.valid := left.valid
    left.ready := right.ready
  }
}
```

顶层只描述级之间的连接：

```scala
StageConnect(fetch.io.out, decode.io.in)
StageConnect(decode.io.out, execute.io.in)
StageConnect(execute.io.out, memory.io.in)
StageConnect(memory.io.out, writeback.io.in)
```

目前 `StageConnect` 是组合连接。未来实现流水线时，可以在该边界插入寄存器或
`Queue`，而各功能模块继续遵守相同的消息协议。

## 3. Verilog 握手接口

实际 Verilator 仿真仍使用 `vsrc` 下的模块化 Verilog。每一级同步增加：

```verilog
input  wire in_valid;
output wire in_ready;
output wire out_valid;
input  wire out_ready;
```

当前各组合级采用：

```verilog
assign in_ready = out_ready;
assign out_valid = in_valid;
```

IFU 是消息源，只在取指响应到达时提供有效指令。WBU 通常可以提交；完整握手测试
模式会随机拉低 WBU ready，使反压传播到存储器响应通道：

```verilog
assign if_out_valid = axi_rvalid && (ifu_state == RESPONSE);
assign wb_in_ready = commit_ready;
```

因此 ready 从 WBU 反向传播到 IFU，valid 从 IFU 正向传播到 WBU。载荷仍使用
Verilog 的离散端口表达，但所有端口共同属于同一条级间消息总线。

## 4. 状态与副作用门控

引入握手后，不能再无条件更新体系结构状态。

WBU 仅在提交握手时更新 PC 和寄存器：

```verilog
if (in_valid && in_ready) begin
  pc_reg <= pc_next;
  if (wb_en && wb_idx != 0)
    regs[wb_idx] <= wb_data;
end
```

无效消息不会产生：

- 寄存器写回。
- 数据存储器读写请求。
- CSR 写入。
- `ecall` 状态更新。
- trap。

CSR 副作用在 EXU 消息成功传入 MEMU 时发生；数据存储器副作用只在 MEMU 的请求拍
发生；`writeback.io.out.fire` 是最终指令提交点。这样下游拉低 `ready` 时，上游消息
和 PC 保持稳定，store 不会重复写入，寄存器和 PC 也不会提前更新。

## 5. 为什么暂不加入流水寄存器

本阶段讲义要求先采用总线思想重构 NPC，而不是立即实现流水线。直接加入流水寄存器
还需要处理：

- 数据相关和旁路。
- 控制相关及流水线冲刷。
- 异常的精确提交。
- 访存等待造成的反压。

当前先固定消息边界和提交语义。后续升级时，功能模块的职责和消息内容可以复用，
控制变化集中在级间连接、相关处理和存储器总线上。

## 6. 从 SimpleBus 到 AXI4-Lite

SimpleBus 过渡版本先建立两条独立握手通道：

```text
request:  reqValid, reqReady, address/write data
response: respValid, respReady, read data
```

这一步消除了 CPU 对固定存储器延迟的假设，并确定以下规则：

1. `valid && !ready` 时发送方必须保持载荷不变。
2. 请求和响应只在 `valid && ready` 时发生。
3. store 只能产生一次外部副作用。
4. load/store 必须收到响应后才能提交。

最终按讲义将外部访存接口替换为 AXI4-Lite 的五个独立通道：

| 通道 | 方向 | 主要信号 | 用途 |
| --- | --- | --- | --- |
| AR | master -> slave | `arvalid/arready/araddr` | 读地址 |
| R | slave -> master | `rvalid/rready/rdata/rresp` | 读数据和响应 |
| AW | master -> slave | `awvalid/awready/awaddr` | 写地址 |
| W | master -> slave | `wvalid/wready/wdata/wstrb` | 写数据 |
| B | slave -> master | `bvalid/bready/bresp` | 写响应 |

IFU 和 LSU 在核心内部仍各自保留 AXI4-Lite master 接口，随后由仲裁器合并为一套
共享接口。仿真 SRAM 只返回 `OKAY`；Xbar 对未映射地址返回 `DECERR`。

## 7. AXI4-Lite IFU

### 8.1 状态机

IFU 使用请求和响应两态：

```text
REQUEST --arvalid && arready--> RESPONSE
RESPONSE --rvalid && rready--> REQUEST
```

- `REQUEST`：保持 PC、`araddr` 和 `arvalid`，直到存储器接受读地址。
- `RESPONSE`：等待 `rvalid`；下游可接收时才拉高 `rready`。
- 响应握手时，指令同时通过级间总线交给 IDU。

PC 只在 WBU 提交时更新，因此请求阻塞时地址自动保持稳定。
IFU 的 `awvalid/wvalid/bready` 恒为 0。IFU 还包含一项必要的响应缓冲：先完成 R
握手并释放仲裁器，再把锁存的指令送给后级，避免 load 指令组合穿透后形成总线死锁。

### 8.2 仿真存储器

C++ 仿真器保存已握手请求，延迟结束后读取 PMEM：

```text
AR handshake
  -> pending request
  -> optional delay
  -> rvalid + rdata + OKAY
  -> hold until R handshake
```

## 8. AXI4-Lite LSU

### 8.1 Load 状态机

MEMU 根据 EXU 消息中的 `dmemValid` 区分普通指令和访存指令：

- 普通指令：在 `idle` 状态直接向 WBU 发送有效消息。
- load：在 `idle` 保持 AR 请求，AR 握手后进入 `readResponse`。
- `readResponse`：等待 R；R 与 WBU 都 ready 时提交 load。

状态转换为：

```text
ordinary: idle --out.fire--> idle
load:     idle --AR fire--> readResponse --R fire--> idle
```

### 8.2 Store 状态机

AXI4-Lite 明确规定 AW 和 W 是相互独立的通道，不能假设二者同周期握手，也不能
假设固定的先后顺序。MEMU 分别使用 `aw_done` 和 `w_done` 记录接收状态：

```text
idle
  -> writeRequest
  -> AW、W 分别握手，任意顺序
  -> 两者均完成后进入 writeResponse
  -> B fire 后提交 store 并回到 idle
```

某一通道握手后立即撤销该通道的 valid，另一通道继续保持 valid 和载荷。只有 AW
和 W 均被 slave 接收后，仿真存储器才执行一次写入并生成 B 响应。store 只有在
`bvalid && bready` 时提交，避免写请求被重复执行或提前提交。

### 8.3 请求和提交分离

load/store 在 R/B 响应拍才提交，所以另有 `commit_mem_*` 观察口保存提交指令
对应的访存信息，供 mtrace 和 DiffTest MMIO 判断使用。不能直接根据 AR/AW/W
通道判断提交，因为请求握手和体系结构提交发生在不同周期。

### 8.4 仿真存储器

C++ 侧只实现外部 SRAM 的读 slave 和写 slave：

```text
read:  AR fire -> optional delay -> R valid -> R fire
write: AW fire --+
                 +-> 两者均完成 -> optional delay -> 写一次 -> B valid -> B fire
       W fire ---+
```

UART 和 CLINT 已移到 RTL，C++ 不再根据 MMIO 地址执行设备副作用。

## 9. 协议验证器

C++ 运行时监视仲裁和 Xbar 之后的 SRAM AR、AW 和 W。当出现：

```text
valid = 1, ready = 0
```

下一周期对应通道的地址、写掩码或写数据必须保持一致，否则立即报错退出。
随机 AXI 模式：

```bash
NPC_AXI_MODE=random NPC_AXI_SEED=7
```

该模式随机各 SRAM 请求通道的 ready、R/B 响应延迟和 WBU ready。程序结束时打印
实际 stall。例如 `load-store`、seed 7：

```text
AXI-Lite stalls: AR=3229 R=237 AW=139 W=168 B=154
```

五个通道均发生阻塞，协议监视器没有发现 `valid && !ready` 期间载荷变化。

## 10. 多周期 DiffTest

单周期实现可以近似认为“一拍一条指令”，加入同步取指后这个假设不再成立。顶层新增
以下提交观察信号：

| 信号 | 含义 |
| --- | --- |
| `commit_valid` | WBU 本周期提交了一条指令 |
| `commit_pc` | 被提交指令的 PC |
| `commit_instr` | 被提交的指令 |
| `commit_next_pc` | 提交后的下一 PC |
| `commit_trap` | 该提交触发 trap |
| `commit_mem_*` | 提交指令对应的访存信息 |

C++ 每个时钟周期都推进 DUT，但只有 `commit_valid` 时才：

1. 输出 itrace/mtrace/ftrace。
2. 让 NEMU REF 执行一条指令。
3. 比较提交后的 GPR 和 PC。

因此 IFU 的请求周期不会错误地让 REF 前进。

NPC 串口位于 PMEM 外，而 RV32E DiffTest REF 配置没有设备。对 MMIO 指令不能直接
调用 `ref_difftest_exec(1)`，否则 REF 会访问越界。此时 DUT 正常提交，然后通过
`npc_difftest_skip_ref()` 将 DUT 的 GPR 和下一 PC 同步给 REF，再继续比较后续指令。

为使启动代码可在 REF 中执行，NEMU 同步支持只读 CSR：

- `mvendorid = 0x79737978`
- `marchid = 22040000`

## 11. 验证

### 11.1 RTL 检查

```bash
cd ~/ysyx-workbench/npc
make lint
```

`make lint` 检查当前权威 Verilog。Chisel 目录是早期级间消息参考，不包含最新
仲裁器、Xbar 和设备，因此不作为当前 B1 顶层验收。

### 11.2 固定延迟 AXI 模式

```bash
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
NPC_AXI_MODE=fixed CCACHE_DISABLE=1 \
  make ARCH=riscv32e-npc ALL="dummy add load-store movsx" difftest
```

- `dummy`：验证 IFU AR/R 和普通指令提交。
- `load-store`：验证 LSU AR/R、AW/W/B 和写掩码。
- `movsx`：验证字节、半字 load 及符号扩展。

四项均通过 DiffTest。

### 11.3 随机 AXI 反压

```bash
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
NPC_AXI_MODE=random NPC_AXI_SEED=7 CCACHE_DISABLE=1 \
  make ARCH=riscv32e-npc \
  ALL="dummy add bit shift if-else load-store movsx" run-batch
```

7 项均 `PASS`，协议监视器未报告载荷变化，并打印非零五通道 stall 计数。

### 11.4 随机 AXI DiffTest

```bash
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
NPC_AXI_MODE=random NPC_AXI_SEED=20260621 CCACHE_DISABLE=1 \
  make ARCH=riscv32e-npc ALL="add load-store movsx" difftest
```

`add`、`load-store` 和 `movsx` 均通过，覆盖普通提交、算术、不同宽度 load/store
和符号扩展。

### 11.5 异常和上下文切换

```bash
cd ~/ysyx-workbench/am-kernels/kernels/yield-os
NPC_AXI_MODE=random NPC_AXI_SEED=17 CCACHE_DISABLE=1 \
  make ARCH=riscv32e-npc run-batch
```

持续输出 `ABAB...`，说明 CSR、异常入口、`mret` 和 Context 切换未受重构影响。

### 11.6 RT-Thread

```bash
cd ~/Templates/rt-thread-am/bsp/abstract-machine
NPC_AXI_MODE=random NPC_AXI_SEED=23 CCACHE_DISABLE=1 \
  make ARCH=riscv32e-npc run-batch
```

RT-Thread 能执行完整预置命令序列，并最终输出：

```text
msh />utest_list
[I/utest] Commands list :
msh />
```

## 12. 仲裁器、Xbar 和设备

系统拓扑为：

```text
IFU --+
      +--> AXI4-Lite arbiter --> Xbar --> external SRAM
LSU --+                              +-> UART  0xa00003f8
                                      +-> CLINT 0xa0000048/4c
```

读仲裁器在 AR 握手时锁存请求来源，并一直保持到对应 R 握手完成。当前 LSU 优先；
多周期核心不会持续并发发起 IFU/LSU 请求，因此该策略不会造成实际饥饿。写通道只有
LSU 一个 master，直接转发。

Xbar 在请求握手时锁存目标设备。写事务先接收 AW，再向目标转发 W，最后等待 B；
这种实现牺牲 AW/W 并发，但完全遵守 AXI4-Lite，并简化了 W 通道无地址时的路由。
未映射访问返回 `DECERR`。

UART 在 AW/W 完成后输出 `wdata[7:0]`，B 握手后结束事务。CLINT 的 64 位 `mtime`
每个 NPC 周期加 1，低、高 32 位分别位于 `0xa0000048`、`0xa000004c`。RV32 软件
采用 high-low-high 顺序读取，避免低 32 位翻转时得到撕裂值。

## 13. 主频与程序性能

Nangate45 + iEDA 的 STA 结果：

| 目标频率 | setup slack | TNS |
| --- | ---: | ---: |
| 375 MHz | +0.013 ns | 0 |
| 380 MHz | -0.022 ns | 负值 |

报告临界路径为 `2.611 ns`，对应 `376.892 MHz`。当前采用 `375 MHz` 作为软件
时间换算频率。`microbench train` 的实测统计为：

```text
cycles=625796246 instructions=196058201 IPC=0.313294
```

AM 报告总时间为 `1668.617 ms`。按 375 MHz 估算，程序执行时间约为
`625796246 / 375000000 = 1.669 s`，两者相符。

复现命令：

```bash
cd ~/ysyx-workbench/npc
make sta YOSYS=/path/to/yosys STA_FREQ_MHZ=375

cd ~/ysyx-workbench/am-kernels/benchmarks/microbench
make ARCH=riscv32e-npc mainargs=train run-batch
```

该 STA 是综合后、理想时钟和零互连延迟下的结果，不等价于布局布线后的签核频率。

## 14. 当前边界

- 当前最多允许每条 IFU/LSU 总线各有一个未完成事务。
- 尚未支持乱序响应和事务 ID。
- 外部 SRAM 仿真 slave 只产生 `OKAY`；Xbar 的 `DECERR` 会转换为
  instruction/load/store access fault。
- 随机延迟由 C++ 仿真存储器提供，RTL 接口不依赖具体延迟。
- `mcycle` 表示 NPC 物理周期，和按指令执行的 NEMU REF 不应直接比较。

## 15. 参考

- B1 总线：<https://ysyx.oscc.cc/docs/2407/b/1.html>
- 调试历史：[`docs/debug-history.md`](debug-history.md)
