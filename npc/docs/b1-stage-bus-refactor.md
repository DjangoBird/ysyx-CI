# B1：NPC 级间总线重构

本文记录 B1 讲义“重构 NPC”、带有效信号的 SimpleBus 和完整握手 SimpleBus 的
实现。范围包含处理器内部级间消息通信、IFU/LSU 请求响应通道、随机反压测试和
多周期 DiffTest；尚未进入 AXI 或 SoC 接入。

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
两个周期；加入 SimpleBus LSU 后，load/store 还需要一个数据访问等待周期。后续模块
的接口和提交语义无需重新设计。

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
assign if_out_valid = imem_resp_valid && (ifu_state == RESPONSE);
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

## 6. 带有效信号的 SimpleBus

第一阶段在请求和响应通道加入 valid，但将 ready 恒置为 1：

```text
request:  reqValid, address/write data
response: respValid, read data
```

只有 `reqValid` 才能登记请求，只有 `respValid` 才能消费返回数据。相较于固定拍数
状态机，CPU 不再假定“下一拍一定有响应”。仿真时使用：

```bash
NPC_BUS_MODE=valid
```

该模式下 request ready 恒为 1，响应在请求握手后的下一周期有效，用于单独验收有效
信号逻辑。

## 7. 完整握手 SimpleBus

最终接口包含两个独立握手通道：

| 通道 | 请求方 | 接收方 | 握手条件 |
| --- | --- | --- | --- |
| request | CPU | 存储器 | `reqValid && reqReady` |
| response | 存储器 | CPU | `respValid && respReady` |

协议约束：

1. 请求未握手时，CPU 必须保持 valid、地址、写数据、写掩码和读写类型不变。
2. 响应未握手时，存储器必须保持 respValid 和返回数据不变。
3. store 只在请求事务被存储器执行时写一次。
4. CPU 只在响应握手后提交访存指令。

完整握手仿真模式：

```bash
NPC_BUS_MODE=full NPC_BUS_SEED=7
```

此模式会随机拉低 request ready、随机延迟 response valid，并随机拉低 WBU ready，
从而让 response ready 也产生反压。`NPC_BUS_SEED` 用于复现同一组时序。

## 8. SimpleBus IFU

### 8.1 状态机

IFU 使用请求和响应两态：

```text
REQUEST --reqValid && reqReady--> RESPONSE
RESPONSE --respValid && respReady--> REQUEST
```

- `REQUEST`：保持 PC 和 `reqValid`，直到存储器接受请求。
- `RESPONSE`：等待 `respValid`；下游可接收时才拉高 `respReady`。
- 响应握手时，指令同时通过级间总线交给 IDU。

PC 只在 WBU 提交时更新，因此请求阻塞时地址自动保持稳定。

### 8.2 仿真存储器

C++ 仿真器保存已握手请求，延迟结束后读取 PMEM：

```text
request handshake
  -> pending request
  -> optional delay
  -> response_valid + response_data
  -> hold until response handshake
```

## 9. SimpleBus LSU

### 9.1 MEMU 状态机

MEMU 根据 EXU 消息中的 `dmemValid` 区分普通指令和访存指令：

- 普通指令：在 `idle` 状态直接向 WBU 发送有效消息。
- load/store：在 `idle` 保持请求，request 握手后进入 `waitResponse`。
- `waitResponse`：等待 response valid，响应与 WBU 都 ready 时提交指令。

状态转换为：

```text
ordinary: idle --out.fire--> idle
memory:   idle --request fire--> waitResponse --response fire--> idle
```

请求未握手及等待响应期间 `in.ready = 0`，因此 EXU 保持完整消息不变。响应拍才拉高
`in.ready` 和 `out.valid`，EXU/MEMU/WBU 完成一次贯穿握手。这保证地址、写数据、
写掩码、目的寄存器和 PC 在等待期间都保持稳定。

### 9.2 请求和提交分离

外部数据存储器接口只在请求拍有效：

```text
dmem_valid, dmem_we, dmem_wmask, dmem_addr, dmem_wdata
```

load/store 在响应拍才提交，所以另有 `commit_mem_*` 观察口保存提交指令对应的
访存信息，供 mtrace 和 DiffTest MMIO 判断使用。不能直接在提交拍读取
`dmem_valid`，因为该信号此时已经无效。

### 9.3 仿真存储器

C++ 侧增加 `dmem_response`：

```text
request handshake:
  保存地址/写数据/掩码
  -> optional delay
  -> 执行一次 pmem_read/pmem_write
  -> 保持 response valid 和返回数据

response handshake:
  MEMU 生成 load 写回数据
  -> WBU 提交
```

store 在请求拍只执行一次。串口也是 MMIO store，因此字符在请求拍输出，指令在下一拍
提交；最终 `msh />` 的逐字符刷新行为保持不变。

## 10. 协议验证器

C++ 运行时对请求通道执行动态检查。当出现：

```text
reqValid = 1, reqReady = 0
```

下一周期的请求地址、读写类型、写掩码和写数据必须保持一致，否则立即报错退出。
完整握手模式在程序结束时打印实际 stall 数。例如 `load-store`、seed 7：

```text
SimpleBus stalls: imem-req=3026 imem-resp=6338
                  dmem-req=419 dmem-resp=362
```

四项均非零，证明测试实际覆盖了请求和响应反压，而不是仅连接了未被使用的 ready。

## 11. 多周期 DiffTest

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

## 12. 验证

### 12.1 RTL 检查

```bash
cd ~/ysyx-workbench/npc
make lint
sbt compile
sbt run
```

三项均通过：手写 Verilog lint、Chisel 编译和 Chisel Verilog elaboration。

### 12.2 有效信号模式

```bash
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
NPC_BUS_MODE=valid CCACHE_DISABLE=1 \
  make ARCH=riscv32e-npc ALL="dummy load-store movsx" run-batch
```

- `dummy`：验证取指有效信号和普通指令提交。
- `load-store`：验证数据请求/响应有效信号和写掩码。
- `movsx`：验证字节、半字 load 及符号扩展。

三项均 `PASS`。

### 12.3 完整握手模式

```bash
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
NPC_BUS_MODE=full NPC_BUS_SEED=7 CCACHE_DISABLE=1 \
  make ARCH=riscv32e-npc \
  ALL="dummy add bit shift if-else load-store movsx" run-batch
```

7 项均 `PASS`，协议监视器未报告请求载荷变化，并打印非零 stall 计数。

### 12.4 完整握手 DiffTest

```bash
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
NPC_BUS_MODE=full NPC_BUS_SEED=20260621 CCACHE_DISABLE=1 \
  make ARCH=riscv32e-npc ALL="add load-store movsx" difftest
```

`dummy`、`add`、`load-store` 和 `movsx` 均通过，覆盖普通提交、MMIO skip、
算术、不同宽度 load/store 和符号扩展。

### 12.5 异常和上下文切换

```bash
cd ~/ysyx-workbench/am-kernels/kernels/yield-os
NPC_BUS_MODE=full NPC_BUS_SEED=17 CCACHE_DISABLE=1 \
  make ARCH=riscv32e-npc run-batch
```

持续输出 `ABAB...`，说明 CSR、异常入口、`mret` 和 Context 切换未受重构影响。

### 12.6 RT-Thread

```bash
cd ~/Templates/rt-thread-am/bsp/abstract-machine
NPC_BUS_MODE=full NPC_BUS_SEED=23 CCACHE_DISABLE=1 \
  make ARCH=riscv32e-npc run-batch
```

RT-Thread 能执行完整预置命令序列，并最终输出：

```text
msh />utest_list
[I/utest] Commands list :
msh />
```

## 13. 当前边界

- 当前最多允许每条 IFU/LSU 总线各有一个未完成事务。
- 尚未支持乱序响应和事务 ID。
- 随机延迟由 C++ 仿真存储器提供，RTL 接口不依赖具体延迟。
- `mcycle` 表示 NPC 物理周期，和按指令执行的 NEMU REF 不应直接比较。

## 14. 参考

- B1 总线：<https://ysyx.oscc.cc/docs/2407/b/1.html>
