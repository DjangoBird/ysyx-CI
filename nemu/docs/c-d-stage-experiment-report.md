# C/D 阶段实验报告：NEMU、AM 与 NPC

本文基于一生一芯 v24.07 官网讲义，从 D 阶段到 C 阶段重新梳理实验要求，并对照
当前仓库实现给出实现说明、问题回答、运行方式和验证方法。

参考页面：

- D1 支持 RV32IM 的 NEMU：<https://ysyx.oscc.cc/docs/2407/d/1.html>
- D2 程序的机器级表示：<https://ysyx.oscc.cc/docs/2407/d/2.html>
- D3 运行时环境：<https://ysyx.oscc.cc/docs/2407/d/3.html>
- D4 用 RTL 实现迷你 RISC-V 处理器：<https://ysyx.oscc.cc/docs/2407/d/4.html>
- D5 设备和输入输出：<https://ysyx.oscc.cc/docs/2407/d/5.html>
- D6 D 阶段流片准备：<https://ysyx.oscc.cc/docs/2407/d/6.html>
- C1 工具和基础设施：<https://ysyx.oscc.cc/docs/2407/c/1.html>
- C2 支持 RV32E 的单周期 NPC：<https://ysyx.oscc.cc/docs/2407/c/2.html>
- C3 调试技巧：<https://ysyx.oscc.cc/docs/2407/c/3.html>
- C4 ELF 文件和链接：<https://ysyx.oscc.cc/docs/2407/c/4.html>
- C5 异常处理和 RT-Thread：<https://ysyx.oscc.cc/docs/2407/c/5.html>

## 1. 实验目标

### 1.1 D 阶段目标

D 阶段从软件模拟器过渡到 RTL 处理器，核心任务是：

| 阶段 | 官网要求 | 当前实现状态 |
| --- | --- | --- |
| D1 | 根据 PA2 阶段 1，实现支持 RV32IM 的 NEMU | 已实现 |
| D2 | 机器级表示，暂无编程内容 | 已学习，无代码任务 |
| D3 | 根据 PA2 的“程序、运行时环境与 AM”理解运行时环境 | 已在 AM/NEMU/NPC 运行链中使用 |
| D4 | 用 RTL 实现 minirv NPC，支持 AM 程序自动运行 | 已完成，并已演进到 RV32E/AXI-Lite NPC |
| D5 | NEMU 完成 PA2 阶段 3 输入输出；NPC 增加串口和时钟 | 已完成基础设备；NPC 当前 UART/CLINT 已 RTL 化 |
| D6 | D 阶段流片准备：SimpleBus、ysyxSoC 接入、Flash 启动 | 当前已完成并超过 SimpleBus，演进到 AXI-Lite；ysyxSoC 接入不作为本次报告验收项 |

### 1.2 C 阶段目标

C 阶段在 D 阶段 NPC 的基础上继续增强基础设施、ISA 和异常能力：

| 阶段 | 官网要求 | 当前实现状态 |
| --- | --- | --- |
| C1 | 根据 PA2 阶段 2 搭建基础设施 | NEMU/NPC 均有 sdb/trace/DiffTest 相关基础 |
| C2 | NPC 搭建 sdb、trace、DiffTest；实现 RV32E；AM 支持 `riscv32e-npc` | 已实现 |
| C3 | 调试技巧，暂无编程内容 | 已通过 trace/DiffTest/debug docs 固化经验 |
| C4 | ELF 和链接，暂无编程内容 | AM 构建过程生成 ELF/bin/txt 并提供 ftrace 符号 |
| C5 | NEMU 完成 PA3.1 自陷和 PA4.1 RT-Thread；NPC 添加 CSR 并启动 RT-Thread | 已实现 |

## 2. Git 证据索引

用以下命令查看阶段性改动：

```sh
git log --oneline --decorate --all -- nemu abstract-machine npc am-kernels
git show --stat c4e4e3c
git show --stat defc96b
git show --stat e81c6b2
git show --stat 55d3ac1
git show --stat d9a43f4
git show --stat 66de2c2
```

主要提交：

| 提交 | 作用 |
| --- | --- |
| `c4e4e3c` | NEMU RV32 基础指令实现 |
| `0ef557e` | AM/NEMU 批处理运行方式 |
| `27ef062` | klib `printf` 支撑 |
| `defc96b` | NEMU trace/ftrace/mtrace 和 AM 默认传 ELF |
| `2c16d95` | NEMU DiffTest 寄存器检查 |
| `e81c6b2` | NEMU/AM 设备 IO 支撑 |
| `927751b` | 早期 minirv/NPC 基础功能 |
| `6e295a8` | AM-NPC 运行链 |
| `55d3ac1` | NPC 迁移到 Chisel RV32E 单周期实现 |
| `d9a43f4` | RV32E 相关实现 |
| `e76b36c` | RV32 上下文切换 |
| `777ed00` | NEMU etrace |
| `2585edc` | PA4/RT-Thread 文档 |
| `66de2c2` | NPC CSR/RT-Thread 启动 |
| `37c8b4c`、`a23de15`、`65ababd`、`ede3849` | 后续总线化重构，当前实现已超过 C/D 要求 |

## 3. 当前代码结构

### 3.1 NEMU

关键路径：

```text
nemu/src/isa/riscv32/inst.c
nemu/src/isa/riscv32/system/intr.c
nemu/src/isa/riscv32/include/isa-def.h
nemu/src/cpu/cpu-exec.c
nemu/src/memory/paddr.c
nemu/src/device/
nemu/src/utils/ftrace.c
nemu/src/cpu/difftest/
nemu/src/isa/riscv32/difftest/dut.c
```

作用：

- `inst.c`：RV32I/M 指令译码与执行。
- `intr.c`：`ecall` 进入异常、保存 `mepc/mcause`、返回 `mtvec`。
- `isa-def.h`：定义 GPR、PC 和 CSR 状态。
- `cpu-exec.c`：统一执行链，处理 itrace、DiffTest、watchpoint。
- `paddr.c`：物理内存读写与 mtrace。
- `device/`：serial、rtc、keyboard、vga、audio、disk 等 MMIO 设备。
- `ftrace.c`：ELF 符号解析和函数调用跟踪。

详细 NEMU 笔记见：

```text
nemu/docs/c-d-stage-nemu-notes.md
nemu/docs/trace.md
nemu/docs/device-io.md
nemu/docs/pa4-stage1-rv32.md
```

### 3.2 Abstract-Machine

关键路径：

```text
abstract-machine/scripts/platform/nemu.mk
abstract-machine/scripts/platform/npc.mk
abstract-machine/scripts/riscv32e-npc.mk
abstract-machine/am/src/riscv/nemu/
abstract-machine/am/src/riscv/npc/
abstract-machine/am/include/arch/riscv.h
```

作用：

- 为 `riscv32-nemu` 和 `riscv32e-npc` 设置编译、链接、镜像生成和运行命令。
- 生成 `.elf/.bin/.txt`，并把 ELF 传给 ftrace。
- 提供 TRM、IOE、CTE、Context 和 `kcontext()`。
- `trap.S` 保存和恢复上下文，支持 `mret` 返回和上下文切换。

### 3.3 NPC

当前 `npc` 的权威实现是 `vsrc` 下的 Verilog。Chisel 目录保留早期参考，不代表
当前最终状态。

关键路径：

```text
npc/vsrc/top.v
npc/vsrc/minirv_core.v
npc/vsrc/npc_if_stage.v
npc/vsrc/npc_id_stage.v
npc/vsrc/npc_ex_stage.v
npc/vsrc/npc_mem_stage.v
npc/vsrc/npc_wb_stage.v
npc/vsrc/npc_csr_file.v
npc/csrc/
npc/README.md
```

当前 NPC 已经从 D/C 阶段的 minirv/RV32E 单周期继续演进到带 AXI4-Lite 的多周期
结构；但 C/D 阶段要求的 RV32E、AM、sdb、trace、DiffTest、CSR、异常和 RT-Thread
能力仍保留并可验证。

相关细节文档：

```text
nemu/docs/c5-npc-rtthread.md
npc/README.md
npc/docs/debug-history.md
npc/docs/b1-stage-bus-refactor.md
npc/docs/b1-completion-checklist.md
```

## 4. D 阶段实现报告

### 4.1 D1：支持 RV32IM 的 NEMU

官网要求：根据 PA2 阶段 1，实现 RV32IM NEMU，完成“取指、译码、执行、更新 PC”的
解释执行链。

当前实现：

```c
int isa_exec_once(Decode *s) {
  s->isa.inst = inst_fetch(&s->snpc, 4);
  return decode_exec(s);
}
```

外层执行链：

```c
static void exec_once(Decode *s, vaddr_t pc) {
  s->pc = pc;
  s->snpc = pc;
  isa_exec_once(s);
  cpu.pc = s->dnpc;
}
```

已实现 RV32I 指令：

```text
addi/slti/sltiu/xori/ori/andi
slli/srli/srai
add/sub/sll/slt/sltu/xor/srl/sra/or/and
lb/lh/lw/lbu/lhu/sb/sh/sw
jal/jalr/beq/bne/blt/bge/bltu/bgeu
lui/auipc
ecall/ebreak/mret/csrrw/csrrs
```

已实现 RV32M 指令：

```text
mul/mulh/mulhsu/mulhu
div/divu/rem/remu
```

边界行为：

- `div/divu` 除数为 0 返回全 1。
- `rem/remu` 除数为 0 返回被除数。
- `INT_MIN / -1` 按 RISC-V 规范处理。
- 每条指令执行后强制 `x0 = 0`。

### 4.2 D2：程序的机器级表示

官网说明本小节暂无编程内容，主要学习 C 程序到机器级表示的转换。

当前实践：

- AM 构建时生成 `.txt` 反汇编文件，便于对照 ELF/bin：

```make
$(OBJDUMP) -d $(IMAGE).elf > $(IMAGE).txt
```

- NEMU/NPC 的 itrace 都通过机器码和反汇编观察执行流。

操作：

```sh
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
make ARCH=riscv32-nemu ALL=dummy run-batch
less build/dummy-riscv32-nemu.txt
```

### 4.3 D3：运行时环境与 AM

官网要求：根据 PA2 中“程序、运行时环境与 AM”完成相关内容。

当前实现：

- `abstract-machine/scripts/platform/nemu.mk` 设置链接地址：

```make
LDFLAGS += --defsym=_pmem_start=0x80000000 --defsym=_entry_offset=0x0
```

- 镜像被加载到 NEMU `RESET_VECTOR = 0x80000000`。
- TRM 提供 `_start -> main -> halt`。
- `halt()` 通过 `ebreak` 把 `a0` 作为退出码交给 NEMU/NPC。

运行方式：

```sh
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
make ARCH=riscv32-nemu ALL=dummy run-batch
```

预期：

```text
nemu: HIT GOOD TRAP at pc = 0x80000030
[         dummy] PASS
```

### 4.4 D4：RTL minirv NPC

官网要求：

- 模块化 RTL：IFU、IDU、EXU、LSU、WBU。
- 先实现 `addi` 和 `jalr`。
- 通过 DPI-C 实现 `ebreak`，让程序决定仿真何时结束。
- 实现完整 minirv：`add/addi/lui/lw/lbu/sw/sb/jalr`。
- 通过 AM 支持 `minirv-npc`，运行 `dummy`。

当前实现状态：

- 早期 minirv 工作在提交 `927751b`、`6e295a8` 中完成。
- 当前 NPC 已演进为 RV32E 多周期结构，仍保留 IF/ID/EX/MEM/WB 分层。
- `ebreak` 由 RTL 识别后通过 trap 端口通知 C++ 仿真环境。
- 当前 PC 复位为 `0x80000000`，符合 AM/NPC 运行环境。

关键模块：

```text
npc/vsrc/npc_if_stage.v
npc/vsrc/npc_id_stage.v
npc/vsrc/npc_ex_stage.v
npc/vsrc/npc_mem_stage.v
npc/vsrc/npc_wb_stage.v
```

关键问题回答：

- 如何实现 `x0` 恒为 0？
  - 读 `x0` 固定返回 0，写回时忽略 `rd == 0`。
- 为什么要用 DPI-C/仿真环境处理内存？
  - D 阶段为了降低 RTL 复杂度，先让 C++ 模拟存储器和设备；后续总线阶段再逐步硬件化。
- 为什么 `ebreak` 比固定仿真周期更通用？
  - 程序自己知道何时结束，仿真器只需运行到 trap，不需要预先知道指令数。

### 4.5 D5：设备和输入输出

官网要求：

- NEMU 根据 PA2 阶段 3 完成输入输出。
- NPC 在仿真环境中实现串口和时钟。
- 运行 hello、RTC 测试和字符版超级玛丽。

当前 NEMU：

- MMIO 分发在 `nemu/src/device/io`。
- 设备包括 serial、rtc、keyboard、vga、audio、disk。
- `paddr_read/write()` 发现地址不在 pmem 时转发给 MMIO。

当前 NPC：

- C/D 阶段：C++ 仿真环境处理串口和 timer。
- 当前 B1 工作区：UART 和 CLINT 已迁移为 RTL AXI4-Lite slave。
- AM 的 `putch()` 写串口 MMIO，timer 读取 `mtime/mcycle`。

运行方式：

```sh
cd ~/ysyx-workbench/am-kernels/tests/am-tests
make ARCH=riscv32-nemu mainargs=t run-batch
make ARCH=riscv32-nemu mainargs=d run
make ARCH=riscv32-nemu mainargs=y run
```

NPC：

```sh
cd ~/ysyx-workbench/am-kernels/tests/am-tests
make ARCH=riscv32e-npc mainargs=t run-batch
```

注意：

- `run` 是交互式，会进入 `(nemu)` 或 `(npc)`，需要输入 `c`。
- `run-batch` 会直接运行。

### 4.6 D6：D 阶段流片准备

官网要求分为两部分：

1. 学习总线，并在 NPC 中实现支持有效信号的 SimpleBus。
2. 接入 ysyxSoC：调整顶层接口、PC 从 Flash `0x30000000` 取指、实现 `flash_read()`，
   运行 hello 和 dummy。

当前状态：

- 仓库历史中已经完成 SimpleBus 重构，并继续演进到 AXI4-Lite。
- 当前 `npc/docs/b1-stage-bus-refactor.md` 记录了 SimpleBus 到 AXI4-Lite 的演进。
- 当前顶层接口已经是单一 SRAM AXI4-Lite，不是 D6 ysyxSoC 的 SimpleBus 接口。
- ysyxSoC 接入不在本次 NEMU/C-D 报告的验证范围内；若要申请 D 阶段流片，应单独按 D6 的 SoC 接口规范复核。

## 5. C 阶段实现报告

### 5.1 C1：工具和基础设施

官网要求：根据 PA2 阶段 2 搭建基础设施。

当前 NEMU 基础设施：

- sdb：`si/info r/x/p/w/d/c/q` 等调试命令。
- itrace：指令执行踪迹。
- mtrace：内存读写踪迹。
- ftrace：函数调用/返回踪迹。
- etrace：异常踪迹。
- DiffTest：DUT/REF API。

当前 NPC 基础设施：

- sdb：单步、寄存器、内存扫描、表达式、监视点。
- trace：`NPC_ITRACE/NPC_MTRACE/NPC_FTRACE/NPC_TRACE`。
- DiffTest：NPC 作为 DUT，NEMU `.so` 作为 REF。

NEMU trace 文档：

```text
nemu/docs/trace.md
```

NPC trace 文档：

```text
npc/README.md
```

### 5.2 C2：支持 RV32E 的单周期 NPC

官网要求：

- NPC 搭建 sdb。
- NPC 添加 itrace/mtrace/ftrace。
- NPC 添加 DiffTest，REF 选择 NEMU shared object。
- 在 AM 中搭建 `riscv32e-npc`。
- 实现 RV32E 指令集。
- 正确运行之前所有测试。

当前实现：

- `npc/csrc/npc_sdb.cpp`：sdb。
- `npc/csrc/npc_trace.cpp`：itrace/mtrace/ftrace。
- `npc/csrc/npc_difftest.cpp`：DiffTest。
- `abstract-machine/scripts/riscv32e-npc.mk` 和 `platform/npc.mk`：AM/NPC 构建运行。
- `npc/vsrc/npc_ex_stage.v`：RV32E 执行语义。

当前 RV32E 已支持：

```text
add/sub/sll/slt/sltu/xor/srl/sra/or/and
addi/slti/sltiu/xori/ori/andi/slli/srli/srai
auipc/lui/jal/jalr
beq/bne/blt/bge/bltu/bgeu
lb/lh/lw/lbu/lhu/sb/sh/sw
fence/fence.i
ebreak/ecall/mret/csrrw/csrrs
```

运行：

```sh
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
make ARCH=riscv32e-npc ALL=dummy run-batch
make ARCH=riscv32e-npc ALL="add bit shift if-else load-store movsx" run-batch
make ARCH=riscv32e-npc ALL="add load-store movsx" difftest
```

关键问题回答：

- 有符号加法和无符号加法的硬件是否不同？
  - 对 `int32_t a + b` 和 `uint32_t a + b`，RISC-V 通常都使用同一条 `add`。加法器不关心 signed/unsigned，区别只体现在比较、除法、扩展等操作。
- RV32E 没有乘除指令，为什么 C 程序还能写乘除法？
  - 编译器会把乘除法编译为 libgcc 中的软件函数，如 `__mulsi3()`，通过加减移位等基础指令模拟。
- Yosys 会自动把多个 `-` 或 `<` 合并成同一个加法器吗？
  - 综合器会做常量传播和部分公共表达式优化，但不能依赖它自动跨多个独立表达式共享同一份运算硬件。若面积是目标，应在 RTL 结构上显式复用 ALU。
- 移位运算会综合成什么？
  - 变量移位通常综合成多级 mux 组成的 barrel shifter；常量移位通常是连线重排。
- 运算符直接综合是否还有改进空间？
  - 有。常见方向包括 ALU 复用、流水切分、减少长组合路径、对常用路径做专门化。

### 5.3 C3：调试技巧

官网说明本小节暂无编程内容，主要学习调试方法。

当前实践：

- 高层：RT-Thread/AM 输出判断行为。
- 函数级：ftrace。
- 指令级：itrace。
- 访存级：mtrace。
- 异常级：etrace。
- 硬件级：Verilator 波形、NPC trace、DiffTest。
- 总线级：AXI 随机反压和协议稳定性检查。

调试历史见：

```text
npc/docs/debug-history.md
```

### 5.4 C4：ELF 文件和链接

官网说明本小节暂无编程内容。

当前实践：

- AM 构建保留 ELF，用于反汇编、符号解析和 ftrace。
- `platform/nemu.mk` 和 `platform/npc.mk` 默认传入：

```make
--ftrace $(IMAGE).elf
```

常用操作：

```sh
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
make ARCH=riscv32-nemu ALL=dummy run-batch
riscv64-linux-gnu-objdump -d build/dummy-riscv32-nemu.elf | less
readelf -S build/dummy-riscv32-nemu.elf
readelf -s build/dummy-riscv32-nemu.elf
```

### 5.5 C5：异常处理和 RT-Thread

官网要求：

1. 在 NEMU 中实现自陷操作：根据 PA3 阶段 1。
2. 在 NEMU 中运行 RT-Thread：根据 PA4 阶段 1，直到启动 RT-Thread。
3. 在 NPC 中添加 CSR：`mcycle/mcycleh/mvendorid/marchid` 和必要 CSR 指令。
4. 在 NPC 中实现 `ecall/mret` 和简单异常处理机制。
5. 在 NPC 中运行 RT-Thread。
6. 可思考并修复最后 `msh />` 不输出的问题。

#### NEMU 侧

CPU 状态：

```c
word_t mstatus, mtvec, mepc, mcause;
```

支持 CSR：

```text
mstatus 0x300
mtvec   0x305
mepc    0x341
mcause  0x342
mvendorid 0xf11 = 0x79737978
marchid   0xf12 = 22040000
```

`ecall`：

```c
s->dnpc = isa_raise_intr(11, s->pc);
```

`mret`：

```c
s->dnpc = cpu.mepc;
```

AM CTE：

- trap 入口保存 GPR、CSR 和 `mepc`。
- `__am_irq_handle()` 识别 `EVENT_YIELD`。
- 返回下一个 Context 指针。
- `trap.S` 用返回值切换 `sp`，再恢复上下文并 `mret`。

验证：

```sh
cd ~/ysyx-workbench/am-kernels/kernels/yield-os
make ARCH=riscv32-nemu run-batch
```

预期持续输出：

```text
ABABABAB...
```

RT-Thread：

```sh
cd ~/Templates/rt-thread-am/bsp/abstract-machine
make ARCH=riscv32-nemu run-batch
```

预期看到 RT-Thread 横幅和 `msh />`。

#### NPC 侧

CSR 文件：

```text
npc/vsrc/npc_csr_file.v
```

支持：

| CSR | 作用 |
| --- | --- |
| `mstatus` | 复位 `0x1800`，可读写 |
| `mtvec` | 异常入口 |
| `mepc` | 异常 PC |
| `mcause` | 异常原因 |
| `mcycle/mcycleh` | 64 位周期计数 |
| `mvendorid` | `0x79737978` |
| `marchid` | `22040000` |

验证：

```sh
cd ~/ysyx-workbench/am-kernels/kernels/hello
make ARCH=riscv32e-npc run-batch
```

关键输出：

```text
mvendorid = 0x79737978, marchid = 22040000
Hello, AbstractMachine!
HIT GOOD TRAP
```

RT-Thread：

```sh
cd ~/Templates/rt-thread-am/bsp/abstract-machine
NPC_AXI_MODE=random NPC_AXI_SEED=23 make ARCH=riscv32e-npc run-batch
```

预期最终输出：

```text
msh />utest_list
[I/utest] Commands list :
msh />
```

最后 `msh />` 的修复思路：

- 早期问题不是 shell 逻辑，而是串口 MMIO 写事务没有被完整保住。
- 在 AXI-Lite 实现中，AW 和 W 必须独立锁存，BVALID 必须保持到 BREADY。
- store 只有收到 B 响应后才能作为提交完成。
- 当前 RTL UART slave 已按该规则实现，随机反压下能稳定输出最后提示符。

## 6. 操作手册

### 6.1 NEMU 配置

运行 AM 程序时，NEMU 必须是 native ELF，而不是 DiffTest REF 的 shared object。

检查：

```sh
cd ~/ysyx-workbench/nemu
rg -n "CONFIG_TARGET|CONFIG_RVE|CONFIG_DEVICE|CONFIG_TRACE" .config include/generated/autoconf.h
```

推荐状态：

```text
CONFIG_TARGET_NATIVE_ELF=y
# CONFIG_TARGET_SHARE is not set
# CONFIG_RVE is not set
CONFIG_DEVICE=y
CONFIG_TRACE=y
```

若不匹配：

```sh
make ISA=riscv32 menuconfig
make ISA=riscv32
```

### 6.2 NEMU 最小验证

```sh
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
make ARCH=riscv32-nemu ALL=dummy run-batch
```

本次复测结果：

```text
nemu: HIT GOOD TRAP at pc = 0x80000030
[         dummy] PASS
```

### 6.3 NEMU 指令和设备验证

```sh
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
make ARCH=riscv32-nemu ALL="add add-longlong bit shift sub mul div rem load-store" run-batch
make ARCH=riscv32-nemu run-batch
```

```sh
cd ~/ysyx-workbench/am-kernels/tests/am-tests
make ARCH=riscv32-nemu mainargs=t run-batch
make ARCH=riscv32-nemu mainargs=d run
make ARCH=riscv32-nemu mainargs=y run
```

### 6.4 NPC 基础验证

```sh
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
make ARCH=riscv32e-npc ALL=dummy run-batch
make ARCH=riscv32e-npc ALL="add bit shift if-else load-store movsx" run-batch
```

### 6.5 NPC DiffTest

```sh
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
make ARCH=riscv32e-npc ALL="add load-store movsx" difftest
```

注意：

- 该命令会构建/使用 NEMU REF `.so`。
- 之后如果要直接运行 NEMU，需要把 NEMU 从 `TARGET_SHARE` 切回 `TARGET_NATIVE_ELF`。

### 6.6 NPC 随机总线反压

当前实现已超过 C/D 阶段，可用随机 AXI 反压做回归：

```sh
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
NPC_AXI_MODE=random NPC_AXI_SEED=7 \
  make ARCH=riscv32e-npc ALL="dummy add bit shift if-else load-store movsx" run-batch
```

### 6.7 RT-Thread

NEMU：

```sh
cd ~/Templates/rt-thread-am/bsp/abstract-machine
make ARCH=riscv32-nemu run-batch
```

NPC：

```sh
cd ~/Templates/rt-thread-am/bsp/abstract-machine
NPC_AXI_MODE=random NPC_AXI_SEED=23 make ARCH=riscv32e-npc run-batch
```

RT-Thread 是常驻 shell，输出最终 `msh />` 后不会自动退出。自动化验证时可以使用
`timeout` 或手动 `Ctrl-C` 结束。

## 7. 常见问题与排查

### 7.1 AM 命令段错误，执行了 `riscv32-nemu-interpreter-so`

原因：NEMU 当前配置是 `CONFIG_TARGET_SHARE=y`，这是给 NPC DiffTest 当 REF 的
shared object，不是普通可执行 NEMU。

处理：

```sh
cd ~/ysyx-workbench/nemu
make ISA=riscv32 menuconfig
```

选择：

```text
Build target = Executable on Linux Native
```

### 7.2 `run` 停在 `(nemu)` 或 `(npc)`

`run` 是交互式模式，需要输入：

```text
c
```

直接运行使用：

```sh
make ... run-batch
```

### 7.3 etrace 没有输出

检查：

```text
CONFIG_TRACE=y
CONFIG_ETRACE=y
TRACE_END 足够大
程序确实执行 ecall 等异常路径
```

普通 `ebreak` 当前主要用于 AM trap 结束，不一定会走 `isa_raise_intr()`。

### 7.4 NPC RT-Thread 最后没有 `msh />`

优先检查：

- UART MMIO store 是否完整提交。
- AXI-Lite AW/W 是否独立握手。
- BVALID 是否保持到 BREADY。
- trace/DiffTest 是否只在 `commit_valid` 推进。
- stdout 是否及时 flush。

当前 B1 版本已经通过 RTL UART 和完整写响应修复该问题。

### 7.5 RV32E 访问 `x16`-`x31`

RV32E 只有 `x0`-`x15`。当前 NPC 对访问 `x16`-`x31` 的非法编码会触发
illegal-instruction trap。AM 构建必须使用 `riscv32e-npc`，不要把 RV32I/M 程序
直接丢给 RV32E NPC。

## 8. 完成度审计

| 要求 | 证据 | 状态 |
| --- | --- | --- |
| D1 RV32IM NEMU | `nemu/src/isa/riscv32/inst.c`，`dummy` PASS | 完成 |
| D3 AM 运行时 | `platform/nemu.mk`、TRM、`halt()`、`ebreak` | 完成 |
| D4 minirv/NPC | 历史提交 `927751b/6e295a8`，当前 RV32E NPC 保留能力 | 完成并演进 |
| D5 NEMU IO | `nemu/src/device`、`device-io.md` | 完成 |
| D5 NPC 串口/时钟 | `npc/README.md`、`c5-npc-rtthread.md` | 完成 |
| D6 SimpleBus | `a23de15/65ababd`、`b1-stage-bus-refactor.md` | 完成并演进到 AXI-Lite |
| D6 ysyxSoC | 当前报告未验证 SoC 接入 | 未作为本次完成项 |
| C1 基础设施 | NEMU/NPC sdb、trace、DiffTest | 完成 |
| C2 RV32E NPC | `npc/vsrc`、`riscv32e-npc` AM、DiffTest | 完成 |
| C3 调试技巧 | `npc/docs/debug-history.md` | 完成学习与沉淀 |
| C4 ELF/链接 | AM 生成 `.elf/.bin/.txt`，ftrace 使用 ELF | 完成学习与实践 |
| C5 NEMU PA3/PA4 | `pa4-stage1-rv32.md` | 完成 |
| C5 NPC CSR/RT-Thread | `c5-npc-rtthread.md`、`npc_csr_file.v` | 完成 |

## 9. 结论

按官网 D 阶段和 C 阶段要求，当前仓库已经完成：

1. RV32IM NEMU 和 AM 运行链。
2. NEMU 的设备、trace、DiffTest、异常和上下文切换。
3. 从 minirv 到 RV32E 的 NPC 实现。
4. NPC 的 sdb、trace、DiffTest 和 AM `riscv32e-npc`。
5. NPC 的 CSR、`ecall/mret`、Context 切换和 RT-Thread 启动。
6. D6 要求的 SimpleBus 已在历史中完成，当前进一步演进到 AXI4-Lite。

当前需要单独注意的是：若目标是 D 阶段流片考核，还需要按 D6 的 ysyxSoC 接口规范
独立复核顶层接口、Flash 启动和 SoC 程序运行；当前主线 NPC 已进入 B1 总线化形态，
接口不再等同于 D6 讲义中的 SimpleBus 接入形态。
