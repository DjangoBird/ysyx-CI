# C2：支持 RV32E 的 NPC

## 学习记录

C2 把 NPC 从能跑少量指令推进到能作为 RV32E 处理器运行 AM 程序，并支持 sdb、trace
和 DiffTest。

## 实现记录

关键文件：

```text
npc/Makefile
npc/csrc/main.cpp
npc/csrc/npc_runtime.h
npc/csrc/npc_runtime.cpp
npc/csrc/npc_memory.h
npc/csrc/npc_memory.cpp
npc/csrc/npc_step.h
npc/csrc/npc_step.cpp
npc/csrc/npc_sdb.h
npc/csrc/npc_sdb.cpp
npc/csrc/npc_trace.h
npc/csrc/npc_trace.cpp
npc/csrc/npc_difftest.h
npc/csrc/npc_difftest.cpp
abstract-machine/scripts/riscv32e-npc.mk
abstract-machine/scripts/platform/npc.mk
npc/vsrc/top.v
npc/vsrc/minirv_core.v
npc/vsrc/minirv_defs.vh
npc/vsrc/npc_id_stage.v
npc/vsrc/npc_ex_stage.v
npc/vsrc/npc_mem_stage.v
npc/vsrc/npc_wb_stage.v
```

已支持 RV32E 基础整数、访存、分支跳转、`ebreak/ecall/mret` 和 CSR 指令。

## 关键代码与讲解

### RV32E 编译和硬件边界

`abstract-machine/scripts/riscv32e-npc.mk` 覆盖编译参数：

```make
COMMON_CFLAGS += -march=rv32e_zicsr -mabi=ilp32e
LDFLAGS       += -melf32lriscv
```

意义：

- 编译器只使用 `x0-x15`。
- 允许生成 CSR 指令。
- 链接成 32 位 RISC-V ELF。

硬件侧也检查 RV32E 边界。`npc_ex_stage.v` 中很多指令都会判断：

```verilog
if (rd_raw < 5'd16 && rs1_raw < 5'd16 && rs2_raw < 5'd16) begin
  ...
end else begin
  illegal_instr = 1'b1;
end
```

这不是多余检查。它能防止错误镜像访问 `x16-x31` 后静默截断成 `x0-x15`，否则 bug
会表现得非常隐蔽。

### load 扩展完整语义

`npc_mem_stage.v` 根据 `funct3` 和地址低位生成最终写回数据：

```verilog
`F3_LB:  wb_data = {{24{selected_byte[7]}}, selected_byte};
`F3_LBU: wb_data = {24'b0, selected_byte};
`F3_LH:  wb_data = load_byte_off[1]
    ? {{16{axi_rdata[31]}}, axi_rdata[31:16]}
    : {{16{axi_rdata[15]}}, axi_rdata[15:0]};
`F3_LHU: wb_data = load_byte_off[1]
    ? {16'b0, axi_rdata[31:16]}
    : {16'b0, axi_rdata[15:0]};
```

调试 `lb/lbu/lh/lhu` 时，重点看三件事：

1. EXU 算出的字节地址低位。
2. MEMU 读出的 32 位对齐 word。
3. 最终符号扩展或零扩展。

### sdb 如何读 NPC 状态

sdb 不窥探 Verilator 内部层级，而是读 `top.v` 暴露的 debug 端口：

```text
dbg_pc_o
dbg_x0_o ... dbg_x15_o
```

因此 RTL 层级重构后，只要顶层 debug 端口保持不变，sdb 就不用改。

常用命令：

```text
info r
si 10
x 4 pc
p a0
w pc
c
```

### trace 如何工作

NPC trace 的输入不是“当前时钟任意信号”，而是提交信号：

```text
commit_pc
commit_instr
commit_next_pc
commit_mem_valid
commit_mem_addr
commit_mem_wdata/rdata
```

`npc_trace.cpp` 根据这些信号输出：

- itrace：PC、机器码、反汇编。
- mtrace：提交访存。
- ftrace：call/ret。

开启：

```sh
NPC_TRACE=1 make ARCH=riscv32e-npc ALL=dummy run-batch
NPC_ITRACE=1 NPC_MTRACE=1 make ARCH=riscv32e-npc ALL=load-store run-batch
```

NPC DiffTest 按提交点推进：

```cpp
if (!dut.rst && current_commit_valid) {
  npc_trace_commit(commit_pc, commit_instr, commit_next_pc,
                   trace_mem_valid, trace_mem_we, trace_mem_wmask,
                   trace_mem_addr, trace_mem_wdata, trace_mem_rdata);
  if (!commit_trap) {
    if (trace_mem_valid && !in_pmem(trace_mem_addr)) {
      npc_difftest_skip_ref(commit_next_pc);
    } else {
      npc_difftest_step(commit_pc, commit_next_pc);
    }
  }
}
```

讲解：

- NPC 后续变成多周期后，不能每个时钟都让 REF 执行。
- 只有 `commit_valid` 表示架构状态真的改变。
- MMIO 指令用 `skip_ref`，避免无设备 REF 访问越界。

## 改动代码详解

### 头文件定义模块边界

`npc/csrc` 的头文件虽然代码少，但它们定义了 C++ runtime 的依赖边界：

| 文件 | 作用 |
| --- | --- |
| `npc_runtime.h` | 暴露 `Vtop dut`、`pmem`、`PMEM_BASE/PMEM_SIZE` |
| `npc_memory.h` | 暴露 `load_img()`、`pmem_read32()`、`pmem_write32()` |
| `npc_step.h` | 暴露 `single_cycle()`、`reset()`、`run_simulation()` |
| `npc_sdb.h` | 暴露 `sdb_mainloop()` 和 batch 模式 |
| `npc_trace.h` | 暴露 trace 初始化和提交点记录接口 |
| `npc_difftest.h` | 暴露 REF 初始化、step、skip_ref |

`npc/vsrc/minirv_defs.vh` 则集中定义 RV32E opcode 和 funct3 常量。把这些常量集中到
头文件里，可以避免 EXU/MEMU 对同一 funct3 使用不同命名或不同编码。

### `riscv32e-npc` AM 架构入口

`riscv32e-npc` 的作用不是简单换一个名字，而是让 AM 用 RV32E ABI 和 NPC 平台脚本
构建程序。这样编译器不会生成访问 `x16`-`x31` 的代码。

关键效果：

```text
ARCH=riscv32e-npc
  -> 使用 riscv32e 编译选项
  -> 链接 AM NPC 平台
  -> 生成 build/*-riscv32e-npc.bin
  -> 调用 npc/build/top 运行
```

如果用 `riscv32-nemu` 的镜像去跑 RV32E NPC，硬件会把访问高 16 个寄存器视作非法
指令，这是符合 RV32E 规范的。

### NPC trace 和 DiffTest 为什么都要看 `commit_valid`

```cpp
bool current_commit_valid = dut.commit_valid;
uint32_t commit_pc = dut.commit_pc;
uint32_t commit_instr = dut.commit_instr;
uint32_t commit_next_pc = dut.commit_next_pc;
```

这些信号来自 RTL 提交点，而不是取指点。多周期后，一条指令可能经历多个时钟周期：
取指等待、执行等待、访存等待、写回等待。只有 `commit_valid` 为真时，架构状态才
真的对外可见。

### MMIO 为什么要 `skip_ref`

```cpp
if (trace_mem_valid && !in_pmem(trace_mem_addr)) {
  npc_difftest_skip_ref(commit_next_pc);
} else {
  npc_difftest_step(commit_pc, commit_next_pc);
}
```

NEMU REF shared object 通常没有 NPC 的 UART/CLINT RTL 设备。如果让 REF 原样执行
MMIO load/store，它会访问越界或得到不同设备状态。这里的策略是：

1. DUT 正常执行 MMIO。
2. 提交后把 DUT 的寄存器和下一 PC 同步给 REF。
3. 下一条普通指令继续对比。

这样既不放弃 DiffTest，又不会被设备建模差异干扰。

### trap 指令不做普通 DiffTest

```cpp
if (!commit_trap) {
  ...
}
```

NPC 的 `ebreak` 会让 PC 停在 trap 指令自身，NEMU 执行 `ebreak` 后 PC 行为可能不同。
trap 是仿真控制协议，不是普通业务指令，因此跳过 trap 本身，只保证 trap 前普通
指令都被比较。

## 运行方式

```sh
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
make ARCH=riscv32e-npc ALL=dummy run-batch
make ARCH=riscv32e-npc ALL="add bit shift if-else load-store movsx" run-batch
make ARCH=riscv32e-npc ALL="add load-store movsx" difftest
```

直接运行 Verilator 产物：

```sh
cd ~/ysyx-workbench/npc
make all
./build/top -b ../am-kernels/tests/cpu-tests/build/dummy-riscv32e-npc.bin
./build/top ../am-kernels/tests/cpu-tests/build/dummy-riscv32e-npc.bin
```

第二条不带 `-b`，会进入 NPC sdb，可手动输入 `si/info r/c`。

## 讲义问题和回答

有符号加法和无符号加法硬件是否不同？

答：加法器不区分 signed/unsigned；区别主要在比较、除法和扩展。

RV32E 没有乘除法，C 程序为什么还能写乘除？

答：编译器会调用 libgcc 软件函数，用基础指令实现乘除。

## Debug 心得

### 场景 1：一运行就 illegal trap

优先怀疑镜像不是 RV32E：

```bash
readelf -h build/program.elf
rg -n "march|mabi" build/*.txt
```

确认 AM 使用：

```text
ARCH=riscv32e-npc
-march=rv32e_zicsr
-mabi=ilp32e
```

如果 RV32I 镜像访问 `x16-x31`，NPC 会按非法指令处理，这是正确行为。不要把硬件改成
静默截断寄存器编号。

### 场景 2：sdb 寄存器和预期不一致

排查：

```text
info r
si 1
info r
```

如果单步后寄存器没变：

1. 该指令是否真的写寄存器。
2. `rd` 是否为 0。
3. `wb_en` 是否为 1。
4. `wb_idx` 是否等于 `rd[3:0]`。
5. 是否因为 access fault/trap 禁止写回。

sdb 读的是顶层 `dbg_x*`，如果 RTL 内部寄存器对但 sdb 不对，查 top debug 端口连接。

### 场景 3：NPC DiffTest mismatch

先看报错：

```text
DiffTest: register mismatch after pc=...
DiffTest: pc mismatch after pc=...
```

步骤：

1. 用 `.txt` 找 `after pc`。
2. 如果是 ALU，查 EXU。
3. 如果是 load，查 MEMU 扩展。
4. 如果是 branch/jump，查 `commit_next_pc`。
5. 如果是 MMIO，确认 `npc_difftest_skip_ref()` 是否被调用。
6. 如果是 trap，确认 `commit_trap` 时没有普通 step REF。

### 场景 4：load-store fixed 失败

fixed 模式下反压少，优先查语义：

```bash
NPC_AXI_MODE=fixed make ARCH=riscv32e-npc ALL=load-store run-batch
```

重点：

- `lb/lbu` 是否选对 byte lane。
- `lh/lhu` 是否根据 `addr[1]` 选低/高半字。
- signed load 是否符号扩展。
- unsigned load 是否零扩展。
- store byte/half 是否生成正确 `wmask/wdata`。

### 场景 5：fixed 通过但 random 失败

这不是 RV32E 语义问题，而是握手问题。排查：

```bash
NPC_AXI_MODE=random NPC_AXI_SEED=7 \
  make ARCH=riscv32e-npc ALL=load-store difftest
```

看：

- `valid && !ready` 期间 payload 是否稳定。
- R/B 响应是否保持到 ready。
- store 是否等 B 后提交。
- `commit_valid` 是否只出现一次。

### 场景 6：trace 没输出

NPC trace 由环境变量控制：

```bash
NPC_TRACE=1 make ARCH=riscv32e-npc ALL=dummy run-batch
NPC_ITRACE=1 NPC_MTRACE=1 make ARCH=riscv32e-npc ALL=load-store run-batch
```

如果没有输出：

1. 是否走 `run-batch` 时环境变量传给了 NPC 进程。
2. 是否真的有 `commit_valid`。
3. 是否设置了 `NPC_TRACE_LOG` 导致输出写入文件。
4. ftrace 是否传入 ELF 符号。
