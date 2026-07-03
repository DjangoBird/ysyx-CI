# C3：调试技巧

## 学习记录

C3 的重点不是“多用 printf”，而是建立分层定位方法。一个复杂系统出错时，先判断
错误发生在哪一层，再选择工具：

```text
应用输出
  -> 函数调用
  -> 指令执行
  -> 访存
  -> 异常
  -> RTL 提交
  -> 总线协议
```

如果层次判断错了，调试会变成随机试错。例如 RT-Thread 少一个 `msh />`，表面看是
shell 输出问题，实际可能是 UART MMIO store 没等 B 响应；`a0` 变成 `0x800001d8`，
表面看是 load 写错，实际可能是测试进入 bad trap 路径后 `auipc a0, 0` 改了它。

## 实现记录

当前可用工具：

| 层次 | 工具 | 适合回答的问题 |
| --- | --- | --- |
| 应用层 | AM/RT-Thread 输出 | 程序是否跑到预期阶段 |
| 函数层 | ftrace | 是否进入/返回了正确函数 |
| 指令层 | itrace、反汇编 `.txt` | 哪条指令后状态变坏 |
| 访存层 | mtrace、AXI debug | 地址、数据、掩码是否正确 |
| 异常层 | etrace、CSR | `mcause/mepc/mtvec` 是否正确 |
| 架构状态 | sdb、DiffTest | GPR/PC 是否和 REF 一致 |
| RTL 层 | Verilator 波形、顶层 debug 端口 | valid/ready、提交、CSR、写回 |
| 协议层 | `NPC_AXI_MODE=random`、stability check | 反压下 payload 是否稳定 |

## 总体排查流程

### 第一步：确认失败点

先拿到最小复现命令。例如：

```bash
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
make ARCH=riscv32-nemu ALL=load-store run-batch

cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
NPC_AXI_MODE=random NPC_AXI_SEED=7 \
  make ARCH=riscv32e-npc ALL=load-store difftest
```

记录：

- 是 NEMU 失败还是 NPC 失败。
- 是 fixed 模式失败还是 random 模式失败。
- 是 bad trap、DiffTest mismatch、卡死、少输出，还是 access fault。
- 最后一个 PC、最后一条 trace、最后一个输出字符。

### 第二步：用反汇编定位 PC

打开 AM 生成的 `.txt`：

```bash
less build/load-store-riscv32-nemu.txt
rg "800001d8" build/load-store-riscv32-nemu.txt
```

判断这个 PC 是：

- 正常测试代码。
- assert/fail/bad trap 路径。
- 异常 handler。
- 线程入口。

不要只看最后寄存器值。很多测试在失败路径里会改 `a0`，最后看到的 `a0` 可能不是
最初出错指令写出来的值。

### 第三步：选工具

| 现象 | 下一步 |
| --- | --- |
| NEMU 指令语义错 | gdb 断 `decode_exec` 或对应 `INSTPAT` |
| NPC 和 NEMU 不一致 | 看 DiffTest `after pc`，再查 DUT 提交信号 |
| load/store 数据错 | 看 mtrace 或 AXI R/W 数据、MEMU 扩展 |
| random 才失败 | 看 `valid && !ready` payload 稳定性 |
| 无提交卡死 | 开 `NPC_AXI_DEBUG`，看卡在哪个通道 |
| 异常不对 | 看 etrace、`mepc/mcause/mtvec` |
| RT-Thread 输出少 | 看 UART AW/W/B 和 `$fflush()` |

## NEMU gdb 调试

### 构建和启动

```bash
cd ~/ysyx-workbench/nemu
make ISA=riscv32 clean
make ISA=riscv32 CFLAGS_BUILD='-O0 -g'

gdb --args build/riscv32-nemu-interpreter \
  -l /tmp/nemu-log.txt \
  ../am-kernels/tests/cpu-tests/build/load-store-riscv32-nemu.bin
```

如果当前 Makefile 不接受 `CFLAGS_BUILD`，仍可直接 gdb 已有二进制；只是局部变量优化后
可能不完整。

### 常用断点

```gdb
b isa_exec_once
b decode_exec
b paddr_read
b paddr_write
b isa_raise_intr
b difftest_step
b nemu/src/isa/riscv32/inst.c:行号
run
```

### 常用查看

```gdb
p/x cpu.pc
p/x cpu.gpr[10]
p/x cpu.mepc
p/x cpu.mcause
p/x s->pc
p/x s->snpc
p/x s->dnpc
p/x s->isa.inst.val
bt
n
s
finish
```

### 例子：查 load 结果为什么错

1. 在 `.txt` 找到出错 load，例如 `lhu a0, 0(s0)`。
2. 在 `decode_exec` 停住，直到 `s->pc` 等于该 PC。
3. 查看源寄存器和立即数，确认地址。
4. `s` 进入 `paddr_read`，查看 `addr` 和 `len`。
5. 返回后看写回到哪个 `rd`，值是多少。
6. 对比 `.txt` 中下一条指令和测试期望值。

如果读出来只有 8 位或符号扩展错，问题在 load 语义；如果地址错，问题在前面计算
基址或立即数；如果读内存数据本身错，看 store 或镜像初始化。

## NEMU trace 使用

### itrace

用途：看每条指令。

```bash
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
make ARCH=riscv32-nemu ALL=load-store run-batch
less build/nemu-log.txt
```

看法：

- 找到 bad trap 前几十条。
- 对照 `.txt` 确认是否跳到了预期位置。
- 如果 PC 突然跳飞，检查 branch/jal/jalr。

### mtrace

用途：看内存访问。

适合问题：

- load/store 地址错。
- 写掩码错。
- 数据大小错。

看法：

```text
mtrace R addr=... len=... data=... pc=...
mtrace W addr=... len=... data=... pc=...
```

先按 PC 对应回反汇编，再判断这次访存是否符合指令语义。

### ftrace

用途：看函数调用层级。

适合问题：

- 进入了 fail/assert 路径。
- 函数返回地址错。
- 递归或回调异常。

必要条件：运行时传入 ELF：

```text
--ftrace build/program.elf
```

### etrace

用途：看异常入口。

适合问题：

- `ecall` 没进 handler。
- `mret` 后又陷入异常。
- access fault 原因不对。

检查项：

```text
cause
epc
target mtvec
```

如果 etrace 没输出，先确认 `CONFIG_TRACE/CONFIG_ETRACE` 和 trace 时间窗口。

## NPC gdb 调试

### 构建和启动

```bash
cd ~/ysyx-workbench/npc
make clean
make all CXXFLAGS='-O0 -g'
gdb --args build/top -b path/to/program.bin
```

### 常用断点

```gdb
b main
b reset
b single_cycle
b run_simulation
b pmem_read32
b pmem_write32
b npc_trace_commit
b npc_difftest_step
b npc_difftest_skip_ref
run
```

### 常用查看

```gdb
p cycle_count
p instruction_count
p/x dut.dbg_pc_o
p/x dut.dbg_x10_o
p dut.commit_valid
p/x dut.commit_pc
p/x dut.commit_instr
p/x dut.commit_next_pc
p dut.commit_trap
p dut.mem_axi_arvalid
p dut.mem_axi_arready
p dut.mem_axi_rvalid
p dut.mem_axi_rready
p dut.mem_axi_awvalid
p dut.mem_axi_awready
p dut.mem_axi_wvalid
p dut.mem_axi_wready
p dut.mem_axi_bvalid
p dut.mem_axi_bready
```

注意：gdb 看 Verilator C++ 模型时，最稳定的是顶层端口。不要依赖深层内部变量名，
Verilator 版本和 RTL 层级变化都可能改名。

## NPC AXI 调试

### fixed 和 random 的分工

fixed：

```bash
NPC_AXI_MODE=fixed ./build/top -b path/to/program.bin
```

用途：先排除指令语义错误。fixed 下 ready 基本恒定，波形简单。

random：

```bash
NPC_AXI_MODE=random NPC_AXI_SEED=7 ./build/top -b path/to/program.bin
```

用途：验证反压和握手。random 下能暴露：

- AW/W 同拍假设。
- R/B valid 没保持。
- `valid && !ready` 时 payload 改变。
- store 提前提交。

### `NPC_AXI_DEBUG`

```bash
NPC_AXI_DEBUG=1 NPC_AXI_MODE=random NPC_AXI_SEED=7 \
  ./build/top -b path/to/program.bin
```

长时间无提交时会打印各通道 valid/ready。按下面表判断：

| 卡住通道 | 意义 | 看哪里 |
| --- | --- | --- |
| AR | 读地址没被接收 | arbiter、xbar、SRAM ready |
| R | 读响应没人接 | IFU/MEMU `rready`、下游反压 |
| AW | 写地址没被接收 | Xbar 写状态、目标设备 |
| W | 写数据没被接收 | W 是否等 AW、slave 是否忙 |
| B | 写响应没人接 | MEMU 是否等待 B 提交 |

### 看波形的顺序

不要一次加几百个信号。先加：

```text
clk/rst
commit_valid/commit_pc/commit_instr
mem_axi_arvalid/arready/araddr
mem_axi_rvalid/rready/rdata/rresp
mem_axi_awvalid/awready/awaddr
mem_axi_wvalid/wready/wdata/wstrb
mem_axi_bvalid/bready/bresp
trap/trap_code
dbg_pc_o
```

判断事务：

- AR fire：`arvalid && arready`。
- R fire：`rvalid && rready`。
- AW fire：`awvalid && awready`。
- W fire：`wvalid && wready`。
- B fire：`bvalid && bready`。

每次 fire 都应该能解释为某条指令的一部分。

## 典型问题排查

### 1. bad trap 后 `a0` 是奇怪地址

不要立刻认为最后一条 load 把 `a0` 写成这个地址。测试框架进入 bad trap 路径后，
可能执行 `auipc a0, 0`，于是 `a0` 会变成当前 PC 附近的地址。

正确做法：

1. 找到最早进入 fail 路径的分支。
2. 看分支前比较的两个值。
3. 回到产生这两个值的指令。
4. 在那条指令刚提交后看寄存器。

### 2. DiffTest mismatch

按报错的 `after pc` 查反汇编：

```bash
rg "8000...." build/*.txt
```

然后分类：

- ALU：查 EXU 计算。
- load：查地址、RDATA、扩展。
- store：查 wmask/wdata 和 B 提交。
- branch/jump：查 `commit_next_pc`。
- MMIO：查是否应该 `skip_ref`。
- trap：确认是否跳过 trap 本身。

### 3. RT-Thread 卡住或少输出

先分清是上下文问题还是 UART 问题。

上下文问题：

```text
ecall 是否提交
mcause 是否为 11
mepc 是否加 4
trap.S 是否 mv sp, a0
mret 后 pc 是否切到新线程
```

UART 问题：

```text
store addr 是否 0xa00003f8
AW/W 是否到 UART
wstrb[0] 是否为 1
UART 是否输出并 flush
BVALID 是否保持到 BREADY
store 是否 B fire 后提交
```

### 4. Access Fault 不工作

运行：

```bash
cd ~/ysyx-workbench/npc
make test-access-fault
```

如果失败：

- instruction fault：看 IFU 是否锁存 `rresp`。
- load fault：看 MEMU `load_access_fault`。
- store fault：看 MEMU `store_access_fault`。
- handler 检查失败：看 `mepc/mcause` 是否写入 CSR。

### 5. random 模式失败，fixed 模式通过

这几乎一定是握手问题，不是普通指令语义。

优先检查：

- `valid && !ready` 时 payload 是否保持。
- R/B 响应是否保持到 ready。
- AW 和 W 是否允许不同周期。
- store 是否只执行一次。
- 下游反压时 CSR/GPR/PC 是否没有提前更新。

## Debug 心得

- 先缩小层次，再选工具。
- fixed 模式查语义，random 模式查协议。
- 最后一个寄存器值不一定是根因，要找第一次错误。
- trace 看历史，gdb 验证表达式，波形看握手。
- 对总线问题，永远用 `fire = valid && ready` 判断事务是否真的发生。
