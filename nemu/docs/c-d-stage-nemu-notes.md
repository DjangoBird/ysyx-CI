# C/D 阶段 NEMU 修改与运行笔记

本文按讲义 `https://ysyx.oscc.cc/docs/2407/d/1.html` 和
`https://ysyx.oscc.cc/docs/ics-pa/2.1.html` 整理当前仓库中和 NEMU 相关的
C/D 阶段工作。重点是：

1. C 阶段/PA2.1 的取指、译码、执行、更新 PC 这条基础执行链。
2. D1 要求的 RV32IM NEMU。
3. 为了验证这些功能补上的 AM 运行方式、trace、DiffTest 和设备支持。

## 讲义基准

D1 的讲义没有单独展开新的编程任务，而是要求“根据 PA 讲义完成 PA2 阶段 1”，
直到 PA2 阶段 1 结束。PA2.1 的核心模型是：

```text
while (1) {
  从 PC 指示的存储器位置取出指令;
  执行指令;
  更新 PC;
}
```

对应到 NEMU 的职责就是实现一个可执行 RV32 指令的解释器，并让 AM 程序能够从
`0x80000000` 开始装载、运行、触发 `ebreak` 结束。

## Git 记录索引

这些提交是整理本文时用到的主要证据，可用 `git show <commit>` 查看具体改动。

```text
c4e4e3c ADD:basic riscv-inst and pass all the test except:hello-str & tring, I think the problem is that we didn't have the device addr
0ef557e Feat: add batch-function
27ef062 Feat: add printf-function
defc96b Feat:trace
2c16d95 ADD:isa-difftest-checkregs
e81c6b2 Add:io
777ed00 NEMU: add RISC-V exception trace
5d8e778 NEMU: support RISC-V vendor and architecture ID CSRs
```

常用查看方式：

```sh
git log --oneline -- nemu abstract-machine am-kernels
git show --stat c4e4e3c
git show --name-only defc96b
git show 777ed00 -- nemu/src/isa/riscv32/system/intr.c nemu/Kconfig
```

## C 阶段/PA2.1：基础执行链

### 取指

入口在 `nemu/src/isa/riscv32/inst.c`：

```c
int isa_exec_once(Decode *s) {
  s->isa.inst = inst_fetch(&s->snpc, 4);
  return decode_exec(s);
}
```

功能说明：

1. `s->snpc` 初始等于当前 `pc`。
2. `inst_fetch()` 从虚拟地址读取 4 字节指令。
3. 目前 RV32 NEMU 走直接映射，`isa_mmu_check()` 返回 `MMU_DIRECT`，所以访存最终落到物理内存或 MMIO。

相关路径：

```text
nemu/src/isa/riscv32/inst.c
nemu/include/cpu/ifetch.h
nemu/src/memory/vaddr.c
nemu/src/memory/paddr.c
```

### 译码

译码集中在 `decode_operand()` 和 `INSTPAT` 匹配表：

```c
static void decode_operand(Decode *s, int *rd, word_t *src1,
    word_t *src2, word_t *imm, int type)
```

当前支持的基础格式：

```text
TYPE_I: rs1 + imm
TYPE_U: imm
TYPE_S: rs1 + rs2 + imm
TYPE_R: rs1 + rs2
TYPE_B: rs1 + rs2
TYPE_N: 无通用操作数
```

这些格式覆盖 RV32I/M 目前用到的算术、访存、跳转、分支和系统指令。

### 执行

`decode_exec()` 先默认顺序执行：

```c
s->dnpc = s->snpc;
```

然后由 `INSTPAT` 执行具体语义。典型例子：

```text
addi  : R(rd) = src1 + imm
lw    : R(rd) = vaddr_read(src1 + imm, 4)
sw    : vaddr_write(src1 + imm, 4, src2)
jal   : 写返回地址并把 dnpc 改为目标地址
jalr  : 写返回地址并跳转到 (src1 + imm) & ~1
ebreak: 调用 NEMUTRAP(pc, a0) 结束程序
```

每条指令执行后都会强制：

```c
R(0) = 0;
```

这是 RISC-V `x0` 恒为 0 的体系结构约束。

### 更新 PC

CPU 执行一条指令的外层入口在 `nemu/src/cpu/cpu-exec.c`：

```c
static void exec_once(Decode *s, vaddr_t pc) {
  s->pc = pc;
  s->snpc = pc;
  isa_exec_once(s);
  cpu.pc = s->dnpc;
}
```

说明：

1. 普通指令不改 `dnpc`，所以 `cpu.pc` 前进到 `snpc = pc + 4`。
2. 跳转、分支、异常指令改写 `dnpc`，所以 `cpu.pc` 跳到目标地址。
3. `ebreak` 不再继续正常更新执行流，而是设置 NEMU trap 状态。

## D 阶段/D1：RV32IM NEMU

D1 的目标是让 NEMU 支持 RV32IM。当前实现集中在
`nemu/src/isa/riscv32/inst.c`。

### RV32I 指令

已经实现的 RV32I 主要类别：

```text
整数立即数: addi, slti, sltiu, xori, ori, andi
移位立即数: slli, srli, srai
整数寄存器: add, sub, sll, slt, sltu, xor, srl, sra, or, and
访存指令  : lb, lh, lw, lbu, lhu, sb, sh, sw
控制流    : jal, jalr, beq, bne, blt, bge, bltu, bgeu
U 型指令  : lui, auipc
系统指令  : ecall, ebreak, mret, csrrw, csrrs
```

### RV32M 指令

已经实现的 M 扩展：

```text
mul, mulh, mulhsu, mulhu
div, divu
rem, remu
```

边界行为：

1. 除数为 0 时，`div/divu` 返回全 1。
2. `rem/remu` 除数为 0 时返回被除数。
3. 有符号除法 `INT_MIN / -1` 按 RISC-V 规范处理溢出。

### CSR 与异常基础

CPU 状态在 `nemu/src/isa/riscv32/include/isa-def.h`：

```c
typedef struct {
  word_t gpr[32];
  vaddr_t pc;
  word_t mstatus, mtvec, mepc, mcause;
} riscv32_CPU_state;
```

`inst.c` 支持的 CSR：

```text
0x300 mstatus
0x305 mtvec
0x341 mepc
0x342 mcause
0xf11 mvendorid = 0x79737978
0xf12 marchid   = 22040000
```

`ecall` 通过 `isa_raise_intr(11, pc)` 进入 M-mode 异常处理；`mret` 返回
`mepc`。异常处理逻辑在 `nemu/src/isa/riscv32/system/intr.c`。

## AM/NEMU 运行方式

AM 的 NEMU 平台脚本在 `abstract-machine/scripts/platform/nemu.mk`。

关键配置：

```make
LDFLAGS += --defsym=_pmem_start=0x80000000 --defsym=_entry_offset=0x0
NEMUFLAGS_BASE += -l $(shell dirname $(IMAGE).elf)/nemu-log.txt
NEMUFLAGS_BASE += --ftrace $(IMAGE).elf
```

含义：

1. AM 镜像链接到 `0x80000000`，和 NEMU `RESET_VECTOR` 对齐。
2. 默认生成 `nemu-log.txt`。
3. 默认把 ELF 传给 NEMU，供 ftrace 解析函数符号。

### 运行前配置检查

运行 AM 程序时，NEMU 必须是 native ELF 可执行目标，不能停在 DiffTest REF 用的
`TARGET_SHARE` 配置。否则 AM 的 Makefile 会把
`build/riscv32-nemu-interpreter-so` 当成可执行文件运行，可能直接段错误。

检查方式：

```sh
cd ~/ysyx-workbench/nemu
rg -n "CONFIG_TARGET|CONFIG_RVE|CONFIG_DEVICE|CONFIG_TRACE" .config include/generated/autoconf.h
```

当前 D1/RV32IM + AM 测试推荐状态：

```text
CONFIG_TARGET_NATIVE_ELF=y
# CONFIG_TARGET_SHARE is not set
# CONFIG_RVE is not set
CONFIG_DEVICE=y
CONFIG_TRACE=y
```

如果配置不对，执行：

```sh
cd ~/ysyx-workbench/nemu
make ISA=riscv32 menuconfig
```

确认：

```text
Build target = Executable on Linux Native
Use E extension = n
Devices = y
Enable tracer = y
```

### 运行 cpu-tests 的 dummy

dummy 是 PA2.1 最小验证程序之一，用来确认 AM 镜像能被 NEMU 装载并正常
`HIT GOOD TRAP`。

```sh
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
make ARCH=riscv32-nemu ALL=dummy run-batch
```

预期：

```text
[         dummy] PASS
```

本次整理时已经重新验证该命令，输出包含：

```text
nemu: HIT GOOD TRAP at pc = 0x80000030
[         dummy] PASS
```

如果想进入 sdb 手动执行：

```sh
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
make ARCH=riscv32-nemu ALL=dummy run
```

进入 `(nemu)` 后输入：

```text
c
```

### 运行全部 cpu-tests

```sh
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
make ARCH=riscv32-nemu run-batch
```

如果只跑一组：

```sh
make ARCH=riscv32-nemu ALL="add load-store div" run-batch
```

### 运行 am-tests

`am-tests` 用 `mainargs` 选择测试项。

```sh
cd ~/ysyx-workbench/am-kernels/tests/am-tests
make ARCH=riscv32-nemu mainargs=t run-batch
make ARCH=riscv32-nemu mainargs=i run
```

常用参数：

```text
t: timer
d: device
y: yield
i: interrupt/context event 相关测试
```

交互式 `run` 会停在 NEMU sdb，需要输入 `c` 才开始执行。

### 直接运行 NEMU

AM 构建后也可以直接把 bin 交给 NEMU：

```sh
cd ~/ysyx-workbench/nemu
make ISA=riscv32 run \
  ARGS="-l ../am-kernels/tests/cpu-tests/build/nemu-log.txt -b" \
  IMG=../am-kernels/tests/cpu-tests/build/dummy-riscv32-nemu.bin
```

## Trace 和调试支撑

trace 相关说明见 `nemu/docs/trace.md`。当前 NEMU 支持：

```text
itrace: 指令执行踪迹
mtrace: 物理内存访问踪迹
ftrace: 函数调用/返回踪迹
etrace: 异常进入踪迹
```

### itrace

位置：

```text
nemu/src/cpu/cpu-exec.c
nemu/src/utils/disasm.c
```

用途：

1. 每条指令执行后生成反汇编文本。
2. 保存最近 32 条指令到 ring buffer。
3. 出错时可以快速回看最近执行流。

### mtrace

位置：

```text
nemu/src/memory/paddr.c
```

输出格式：

```text
mtrace R addr=... len=... data=... pc=...
mtrace W addr=... len=... data=... pc=...
```

### ftrace

位置：

```text
nemu/src/utils/ftrace.c
nemu/src/monitor/monitor.c
abstract-machine/scripts/platform/nemu.mk
```

运行 AM 程序时，`nemu.mk` 默认加：

```text
--ftrace $(IMAGE).elf
```

所以只要 `CONFIG_FTRACE=y`，NEMU 就能解析 ELF 函数符号并输出调用/返回关系。

### etrace

位置：

```text
nemu/Kconfig
nemu/src/isa/riscv32/system/intr.c
```

功能：

1. 在 NEMU 侧记录异常，不影响客户程序行为。
2. `ecall`、后续异常或中断进入 `isa_raise_intr()` 时输出 cause、epc、target。

输出格式：

```text
etrace cause=0x0000000b (environment call from M-mode) epc=... target=...
```

开启方式：

```sh
cd ~/ysyx-workbench/nemu
make ISA=riscv32 menuconfig
```

在 `Testing and Debugging` 中打开：

```text
TRACE
ETRACE
```

然后重新运行 AM 测试，查看对应 `build/nemu-log.txt`。

## DiffTest 支撑

DiffTest 相关改动来自 `2c16d95`，主要路径：

```text
nemu/src/cpu/difftest/dut.c
nemu/src/isa/riscv32/difftest/dut.c
nemu/src/cpu/cpu-exec.c
```

作用：

1. NEMU 作为 DUT 时，每条指令后调用参考模型比对寄存器。
2. NEMU 作为 REF 时，通过 `difftest_memcpy/regcpy/exec/raise_intr` 暴露接口。
3. `isa_difftest_checkregs()` 检查 RV32 通用寄存器和 PC。

常用命令需要先按环境准备 Spike/QEMU 参考库，之后使用：

```sh
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
make ARCH=riscv32-nemu ALL=dummy difftest
```

如果没有配置参考模型，先用 `run-batch` 做基础验收。

## 设备和 IO 支撑

PA2.1 的最小 dummy 不依赖复杂设备，但运行 `hello-str`、`am-tests` 等程序需要
NEMU 的设备/MMIO 支撑。相关提交是 `e81c6b2 Add:io`。

主要路径：

```text
nemu/src/device/io/map.c
nemu/src/device/io/mmio.c
nemu/src/device/io/port-io.c
abstract-machine/am/src/platform/nemu/ioe/timer.c
abstract-machine/am/src/platform/nemu/ioe/input.c
```

文档见：

```text
nemu/docs/device-io.md
```

用途：

1. 把串口、时钟、键盘、VGA、音频等设备映射到 MMIO 地址。
2. CPU 访问非 pmem 地址时通过 `mmio_read/mmio_write` 分发到设备。
3. AM 的 IOE 层通过这些地址实现 `io_read/io_write`。

## 常见问题

### 程序停在 `(nemu)` 不执行

`make ... run` 是交互式运行，需要手动输入：

```text
c
```

如果想直接执行，使用：

```sh
make ARCH=riscv32-nemu ... run-batch
```

### 看不到 etrace 输出

检查三点：

1. `CONFIG_TRACE=y`。
2. `CONFIG_ETRACE=y`。
3. 程序确实执行了会进入 `isa_raise_intr()` 的路径，例如 `ecall`。

`yield` 或普通 `ebreak` 不一定等价于异常进入；`ebreak` 在当前实现中主要用于
`NEMUTRAP` 结束程序。

### `hello-str` 或设备测试失败

先确认是否打开设备：

```sh
cd ~/ysyx-workbench/nemu
make ISA=riscv32 menuconfig
```

需要 `CONFIG_DEVICE=y`，并确认 NEMU 启动时打印了 serial、rtc、keyboard 等
MMIO map。

## 阶段验收命令清单

建议按从小到大的顺序验证。

```sh
# 1. NEMU 构建
cd ~/ysyx-workbench/nemu
make ISA=riscv32

# 2. PA2.1 最小程序
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
make ARCH=riscv32-nemu ALL=dummy run-batch

# 3. 基础指令集
make ARCH=riscv32-nemu ALL="add add-longlong bit shift sub mul div rem load-store" run-batch

# 4. 全量 cpu-tests
make ARCH=riscv32-nemu run-batch

# 5. AM timer/device/yield 类测试
cd ~/ysyx-workbench/am-kernels/tests/am-tests
make ARCH=riscv32-nemu mainargs=t run-batch
make ARCH=riscv32-nemu mainargs=d run
make ARCH=riscv32-nemu mainargs=y run
```

日志位置通常在对应测试目录的：

```text
build/nemu-log.txt
```

## 当前结论

按 D1 与 PA2.1 的基准，当前 NEMU 已经具备：

1. RV32 取指、译码、执行、更新 PC 的解释执行链。
2. RV32I 基础整数、访存、跳转、分支和系统指令。
3. RV32M 乘除法扩展。
4. AM/NEMU 镜像装载、批处理运行和交互运行方式。
5. `itrace/mtrace/ftrace/etrace` 调试支撑。
6. 可继续支撑后续 PA3/PA4 的 CSR、异常、设备、DiffTest 基础设施。
