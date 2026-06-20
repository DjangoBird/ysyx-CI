# B1：NPC 级间总线重构

本文记录 B1 讲义“重构 NPC”和“支持 SimpleBus 的 IFU”部分的实现。范围包含
处理器内部级间消息通信、固定一周期取指总线和多周期 DiffTest；尚未实现
SimpleBus LSU、完整握手版本、AXI 或 SoC 接入。

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

完成第一步重构时仍保持单周期行为。加入 SimpleBus IFU 后，每条普通指令至少需要
两个周期，但后续模块的接口和提交语义无需重新设计。

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

IFU 是消息源，每周期提供有效指令；WBU 是最终消费者，当前始终可以提交：

```verilog
assign if_out_valid = 1'b1;
assign wb_in_ready = 1'b1;
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

Chisel 顶层用 `execute.io.out.fire` 门控访存和 CSR 副作用，用
`writeback.io.out.fire` 作为指令提交点。这样即使以后下游拉低 `ready`，上游消息
和 PC 也会保持，不会重复提交或丢失副作用。

## 5. 为什么暂不加入流水寄存器

本阶段讲义要求先采用总线思想重构 NPC，而不是立即实现流水线。直接加入流水寄存器
还需要处理：

- 数据相关和旁路。
- 控制相关及流水线冲刷。
- 异常的精确提交。
- 访存等待造成的反压。

当前先固定消息边界和提交语义。后续升级时，功能模块的职责和消息内容可以复用，
控制变化集中在级间连接、相关处理和存储器总线上。

## 6. SimpleBus IFU

### 6.1 时序

讲义中的第一版 SimpleBus 只有取指地址和返回数据，存储器固定在下一周期返回：

```text
cycle N:     IFU 输出 PC，发起取指
cycle N + 1: IFU 接收指令，向 IDU 发送有效消息
```

IFU 使用两态状态机：

```text
idle -> waitResponse -> idle
```

- `idle`：地址已经由 `imem_addr` 输出，本周期末登记存储器响应。
- `waitResponse`：`out.valid = 1`，等待下游 `ready`。
- `out.fire`：指令成功交给后续模块，返回 `idle` 请求下一条指令。

PC 只在 WBU 提交时更新，所以 `waitResponse` 遭遇反压时地址和 PC 都保持稳定。

### 6.2 仿真存储器

C++ 仿真环境使用 `imem_response` 保存同步读响应：

```text
本周期输入 imem_response
  -> RTL 计算并输出 imem_addr
  -> 时钟上升沿
  -> imem_response = pmem_read32(imem_addr)
  -> 下一周期送回 RTL
```

本阶段只修改 IFU。LSU 仍是组合访问，因此拉低时钟求值出 `dmem_addr` 后，需要立即
回填 `dmem_rdata` 并再次求值，确保 load 在提交前得到正确数据。

## 7. 多周期 DiffTest

单周期实现可以近似认为“一拍一条指令”，加入同步取指后这个假设不再成立。顶层新增
以下提交观察信号：

| 信号 | 含义 |
| --- | --- |
| `commit_valid` | WBU 本周期提交了一条指令 |
| `commit_pc` | 被提交指令的 PC |
| `commit_instr` | 被提交的指令 |
| `commit_next_pc` | 提交后的下一 PC |
| `commit_trap` | 该提交触发 trap |

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

## 8. 验证

### 8.1 RTL 检查

```bash
cd ~/ysyx-workbench/npc
make lint
sbt compile
sbt run
```

三项均通过：手写 Verilog lint、Chisel 编译和 Chisel Verilog elaboration。

### 8.2 RV32E 指令回归

```bash
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
CCACHE_DISABLE=1 make ARCH=riscv32e-npc \
  ALL="dummy add bit shift if-else load-store movsx" run-batch
```

7 项测试均 `PASS`。

### 8.3 多周期 DiffTest

```bash
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
CCACHE_DISABLE=1 make ARCH=riscv32e-npc ALL=dummy difftest
CCACHE_DISABLE=1 make ARCH=riscv32e-npc \
  ALL="add load-store" difftest
```

`dummy`、`add` 和 `load-store` 均通过，覆盖普通提交、MMIO skip、算术和访存。

### 8.4 异常和上下文切换

```bash
cd ~/ysyx-workbench/am-kernels/kernels/yield-os
CCACHE_DISABLE=1 make ARCH=riscv32e-npc run-batch
```

持续输出 `ABAB...`，说明 CSR、异常入口、`mret` 和 Context 切换未受重构影响。

### 8.5 RT-Thread

```bash
cd ~/Templates/rt-thread-am/bsp/abstract-machine
CCACHE_DISABLE=1 make ARCH=riscv32e-npc run-batch
```

RT-Thread 能执行完整预置命令序列，并最终输出：

```text
msh />utest_list
[I/utest] Commands list :
msh />
```

## 9. 当前边界

- IFU 采用固定一周期响应，尚无 `reqValid/reqReady/respValid/respReady`。
- LSU 仍为组合访问，尚未按 SimpleBus 改造成多周期。
- 尚未加入随机存储器延迟。
- `mcycle` 表示 NPC 物理周期，和按指令执行的 NEMU REF 不应直接比较。

## 10. 参考

- B1 总线：<https://ysyx.oscc.cc/docs/2407/b/1.html>
