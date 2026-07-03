# C2：支持 RV32E 的 NPC

## 学习记录

C2 把 NPC 从能跑少量指令推进到能作为 RV32E 处理器运行 AM 程序，并支持 sdb、trace
和 DiffTest。

## 实现记录

关键文件：

```text
npc/csrc/npc_sdb.cpp
npc/csrc/npc_trace.cpp
npc/csrc/npc_difftest.cpp
abstract-machine/scripts/riscv32e-npc.mk
abstract-machine/scripts/platform/npc.mk
npc/vsrc/npc_ex_stage.v
```

已支持 RV32E 基础整数、访存、分支跳转、`ebreak/ecall/mret` 和 CSR 指令。

## 关键代码与讲解

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

## 讲义问题和回答

有符号加法和无符号加法硬件是否不同？

答：加法器不区分 signed/unsigned；区别主要在比较、除法和扩展。

RV32E 没有乘除法，C 程序为什么还能写乘除？

答：编译器会调用 libgcc 软件函数，用基础指令实现乘除。

## Debug 心得

- RV32E 只有 `x0`-`x15`，不要把 RV32I/M 镜像直接给 RV32E NPC。
- trap 指令本身 PC 行为可能和 NEMU 不同，DiffTest 应跳过 trap 本身。
- MMIO 指令不要让 REF 原样执行。
