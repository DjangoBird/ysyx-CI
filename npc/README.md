# NPC 模拟器

本目录包含同步维护的 Chisel 设计源和用于 Verilator 仿真的模块化 Verilog。
当前目标是 RV32E 单周期实现，硬件内部按取指、译码、执行、访存、写回、
寄存器堆和 PC 模块拆开，C++ 侧提供内存模型、单步时钟推进和 sdb 调试器。

## 先决条件

- Linux 环境
- 安装工具：`git`, `make`, `g++`, `verilator`, `python3`
- 如果需要 GUI（NVBoard / SDL 显示），还需安装：`libsdl2-dev` 以及 NVBoard 的相关依赖

示例（Debian/Ubuntu）快速安装：

```bash
sudo apt update
sudo apt install -y git build-essential verilator python3 python3-pip libsdl2-dev
```

获取代码：

```bash
git clone <your-repo-url>
cd ysyx-workbench/npc
```

## 当前设计思路

### 硬件部分

Verilog 顶层是 [vsrc/top.v](vsrc/top.v)。顶层模块名固定为 `top`，端口包括：

- `clk/rst/led`
- 指令存储器接口：`imem_addr`, `imem_rdata`
- 数据存储器接口：`dmem_valid`, `dmem_we`, `dmem_wmask`, `dmem_addr`, `dmem_wdata`, `dmem_rdata`
- trap 接口：`trap`, `trap_code`
- sdb 调试寄存器端口：`dbg_x0_o` 到 `dbg_x15_o`

当前 NPC 是 RV32E 简单多周期结构。IFU 通过固定一周期延迟的 SimpleBus 取指：
第一周期发送地址，第二周期接收指令并完成后续组合执行和提交。PC 复位地址为
`0x80000000`，通用寄存器为 RV32E 规定的 `x0`-`x15`，其中 `x0` 恒为 0。

各执行级之间使用 `valid/ready` 握手接口传递消息。当前写回级始终 ready，因此每个
条有效指令在返回周期完成后续握手；PC、寄存器、CSR、访存和异常副作用都由有效消息
或最终提交握手门控。Chisel 实现使用 `Decoupled` Bundle 和
`StageConnect`，Verilog 实现使用对应的显式 `in_valid/in_ready`、
`out_valid/out_ready` 端口。

模块分工：

- [vsrc/minirv_defs.vh](vsrc/minirv_defs.vh)：集中定义 RV32E opcode 和 funct3 常量。
- [vsrc/top.v](vsrc/top.v)：顶层封装，连接核心、内存接口、trap 接口和调试寄存器端口。
- [vsrc/minirv_core.v](vsrc/minirv_core.v)：通过逐级握手总线连接取指、译码、执行、访存、写回各级。
- [vsrc/npc_if_stage.v](vsrc/npc_if_stage.v)：输出取指地址、取回指令并计算 `pc + 4`。
- [vsrc/npc_id_stage.v](vsrc/npc_id_stage.v)：解析 opcode、rd、rs1、rs2、funct3、funct7 和 I/S/B/U/J 立即数，并识别 `ebreak`。
- [vsrc/npc_ex_stage.v](vsrc/npc_ex_stage.v)：实现完整 RV32E 基础整数 ISA 的执行、访存请求、PC 跳转和 trap 控制。
- [vsrc/npc_mem_stage.v](vsrc/npc_mem_stage.v)：处理 load 写回数据，完成字节/半字/字访问的符号扩展或零扩展。
- [vsrc/npc_wb_stage.v](vsrc/npc_wb_stage.v)：保存 PC 和 16 个 32-bit 通用寄存器，并完成写回。

已实现的 RV32E 基础指令包括：

- 整数寄存器计算：`add`, `sub`, `sll`, `slt`, `sltu`, `xor`, `srl`, `sra`, `or`, `and`
- 整数立即数计算：`addi`, `slti`, `sltiu`, `xori`, `ori`, `andi`, `slli`, `srli`, `srai`
- PC/跳转/分支：`auipc`, `lui`, `jal`, `jalr`, `beq`, `bne`, `blt`, `bge`, `bltu`, `bgeu`
- 访存：`lb`, `lh`, `lw`, `lbu`, `lhu`, `sb`, `sh`, `sw`
- 内存屏障：`fence`, `fence.i`。当前 NPC 是单周期顺序执行模型，因此这两条按 no-op 处理并顺序推进 PC。
- 环境指令：`ebreak`, `ecall`, `mret`。`ebreak` 使用 `a0` 作为 AM 退出码；
  `ecall` 保存异常状态并跳转到 `mtvec`。

RV32E 不包含 `x16`-`x31`。硬件执行级会把访问 `x16`-`x31`、非法 funct3/funct7、未知 opcode、未实现 SYSTEM 指令等编码报告为 illegal-instruction trap，`trap_code = 2`。`trap_code = 0` 仍表示 AM good trap，`trap_code = 1` 用于当前简化模型下的 `ecall`。

### C++ 仿真运行时

C++ 运行时在 [csrc](csrc) 下：

- [main.cpp](csrc/main.cpp)：程序入口。解析命令行镜像路径，初始化内存，reset DUT，然后进入 sdb。
- [npc_runtime.h](csrc/npc_runtime.h) / [npc_runtime.cpp](csrc/npc_runtime.cpp)：定义 Verilator 生成的 `Vtop dut`、GUI 状态和物理内存数组 `pmem`。
- [npc_memory.h](csrc/npc_memory.h) / [npc_memory.cpp](csrc/npc_memory.cpp)：实现 `pmem_read32()`、`pmem_write32()` 和 `load_img()`。
- [npc_step.h](csrc/npc_step.h) / [npc_step.cpp](csrc/npc_step.cpp)：实现 `single_cycle()`、`reset()` 和连续运行逻辑。
- [npc_sdb.h](csrc/npc_sdb.h) / [npc_sdb.cpp](csrc/npc_sdb.cpp)：实现交互式 sdb。
- [npc_difftest.h](csrc/npc_difftest.h) / [npc_difftest.cpp](csrc/npc_difftest.cpp)：通过 `dlopen()` 加载 NEMU REF so，执行逐指令寄存器/PC 对比。

内存模型从 `PMEM_BASE = 0x80000000` 开始，大小为 128 MiB。镜像文件会被 `load_img()` 直接加载到 `pmem[0]`，因此镜像的第一个字节对应客户机地址 `0x80000000`。

`single_cycle()` 的核心流程是：

1. 将上一周期登记的指令响应送入 `imem_rdata`。
2. 拉低 `clk` 并求值，得到本周期取指地址和 LSU 地址。
3. 组合回填 `dmem_rdata`，再次求值使 load 数据稳定。
4. 记录 `commit_valid/pc/instr/next_pc`，再推进时钟上升沿。
5. 将本周期 `imem_addr` 对应的数据登记为下一周期的指令响应。
6. 仅在 `commit_valid` 时执行 trace 和 DiffTest。

### DiffTest 支持

NPC 的 DUT 是 Verilog/Verilator 构建出的 `build/top`，REF 使用 NEMU 生成的共享库：

```text
../nemu/build/riscv32-nemu-interpreter-so
```

NEMU REF 侧实现了 `difftest_memcpy()`、`difftest_regcpy()` 和 `difftest_exec()`。NPC 启动 DiffTest 后会：

1. `dlopen()` NEMU REF so，并解析 `difftest_init/memcpy/regcpy/exec`。
2. 把当前镜像从 NPC `pmem` 拷贝到 REF。
3. reset 后把 NPC 的 16 个 RV32E GPR 和 PC 同步给 REF。
4. 每个时钟周期检查 `commit_valid`；只有提交一条普通指令时，才让 REF 执行
   1 条并比较 GPR/PC。
5. MMIO 指令无法在无设备的 REF 中执行，DUT 提交后使用 `skip_ref` 将 DUT 状态
   同步到 REF。

当前 NPC 的 `ebreak` 会让 PC 停在 trap 指令本身，而 NEMU 执行 `ebreak` 后 PC 会进入下一条，因此 trap 指令本身不做 DiffTest 对比；trap 前的普通指令都会检查。

构建 NEMU REF so：

```bash
make -C ../nemu riscv32e-ref_defconfig
make -C ../nemu
```

也可以让 NPC 在 `NPC_DIFF=1` 时自动构建 REF so：

```bash
make NPC_DIFF=1 run ARGS=-b IMG=path/to/img.bin
```

手动指定 REF：

```bash
make run ARGS="-b -d ../nemu/build/riscv32-nemu-interpreter-so" IMG=path/to/img.bin
```

注意：`riscv32e-ref_defconfig` 会把 NEMU 切换到 shared object 目标。如果之后要把 NEMU 编回普通 ELF，需要重新选择 NEMU 的 build target，例如重新运行对应 defconfig 或 `make menuconfig`。


### trace 支持

NPC 侧实现了模仿 NEMU 的基础 trace：

- `itrace`：记录每条执行指令的 PC、机器码和 capstone 反汇编。当前指令由 Verilator DUT 侧内存接口提交给 C++ trace 逻辑。
- `mtrace`：记录数据存储器读写，包括地址、数据、写掩码和访问长度。
- `ftrace`：记录 `jal/jalr` 风格的函数调用和 `ret` 返回。当前硬件主要支持 `jalr`，所以 ftrace 对 `jalr rd=x1/x5` 视作 call，对 `jalr x0, 0(x1/x5)` 视作 ret。

trace 默认关闭，用环境变量开启：

```bash
NPC_ITRACE=1 make run IMG=path/to/img.bin
NPC_MTRACE=1 make run IMG=path/to/img.bin
NPC_FTRACE=1 make run IMG=path/to/img.bin
NPC_TRACE=1  make run IMG=path/to/img.bin   # 同时开启 itrace/mtrace/ftrace
```

输出到文件：

```bash
NPC_TRACE=1 NPC_TRACE_LOG=trace.log make run IMG=path/to/img.bin
```

如果有 ELF 文件可用于符号解析，可传给 ftrace：

```bash
NPC_FTRACE=1 NPC_FTRACE_ELF=path/to/program.elf make run IMG=path/to/program.bin
```

如果没有 ELF 符号，ftrace 会退化为打印目标地址。itrace 使用 `../nemu/tools/capstone/repo/libcapstone.so.5`，Makefile 会自动加入 include、链接和 rpath。

示例输出：

```text
itrace 0x8000000c: 23 20 a1 00 sw	a0, 0(sp)
mtrace W addr=0x80000000 len=4 data=0x0000000c mask=0xf
0x80000008: call [0x80000010]
0x80000014: ret  [0x8000000c]
```

### sdb 调试器

sdb 默认启动。它不依赖 readline，只使用标准输入输出，所以可以交互使用，也可以用管道喂命令做自动测试。

寄存器读取通过 Verilog 顶层公开的 `dbg_x0_o` 到 `dbg_x15_o` 端口完成，不依赖 Verilator 内部层级名。这样修改 RTL 层级或升级 Verilator 时更稳。

sdb 命令：

```text
help              显示命令帮助
c                 连续执行直到 trap、监视点触发或仿真结束
q                 退出 NPC
si [N]            单步执行 N 条指令，默认 1 条；执行后会检查监视点
info r            打印 pc 和 RV32E 通用寄存器 x0-x15
info w            打印当前监视点
x N EXPR          从 EXPR 指定地址开始扫描 N 个 32-bit word
p EXPR            计算表达式
w EXPR            设置监视点
d N               删除编号为 N 的监视点
```

表达式支持十进制/十六进制整数、`pc`、`x0`-`x15`、ABI 名称（如 `a0`、`sp`）、括号、`+ - * /`、`== != < <= > >=`、`&& ||`、`& | ^`、一元 `- ! ~`，以及 `*EXPR` 按 32-bit word 解引用内存。

示例：

```text
(npc) info r
(npc) p $a0 + 4
(npc) p *0x80000000
(npc) w a0
(npc) si
(npc) info w
(npc) d 0
(npc) x 4 pc
(npc) c
```

## 构建

```bash
make all
```

构建流程：

1. `verilator` 读取 [vsrc](vsrc) 下的手写 Verilog 和 `csrc/*.cpp`。
2. 生成并编译 C++ 仿真模型，输出 `build/top`。

检查生成 Verilog：

```bash
make lint
```

检查并生成 Chisel 版本：

```bash
sbt compile
sbt run
```

总线化重构的接口、握手规则和扩展方式见
[docs/b1-stage-bus-refactor.md](docs/b1-stage-bus-refactor.md)。

## 推荐测试顺序

建议按下面顺序测试 NPC，先排除构建和工具链问题，再检查 AM 运行，最后打开 DiffTest。

1. 检查 RTL lint：

```bash
cd /home/django/ysyx-workbench/npc
make lint
```

2. 构建 Verilator 模型：

```bash
make all
```

3. 跑一个最小 AM cpu-test：

```bash
cd /home/django/ysyx-workbench/am-kernels/tests/cpu-tests
make ARCH=riscv32e-npc ALL=dummy run-batch
```

4. 跑一组基础指令测试：

```bash
make ARCH=riscv32e-npc ALL="add bit shift if-else load-store movsx" run-batch
```

5. 打开 DiffTest 跑单个测试：

```bash
make ARCH=riscv32e-npc ALL=dummy difftest
```

`difftest` 会让 NPC 自动构建并使用 `../nemu/build/riscv32-nemu-interpreter-so`。注意这会把 NEMU 配置切到 `TARGET_SHARE`，之后如果要直接运行 NEMU，需要把 NEMU 配置切回 `TARGET_NATIVE_ELF`。

## 运行

`IMG` 是要加载到 NPC 客户机内存的裸机程序镜像。它会被放到客户机物理地址 `0x80000000` 开始的位置。

推荐通过 Makefile 运行：

```bash
make run IMG=path/to/img.bin
```

等价于：

```bash
NPC_GUI=0 build/top path/to/img.bin
```

NPC 的运行参数模仿 NEMU，`ARGS` 会排在 `IMG` 前面：

```bash
make run ARGS="-b" IMG=path/to/img.bin
make run ARGS="-b -d ../nemu/build/riscv32-nemu-interpreter-so" IMG=path/to/img.bin
```

常用参数：

```text
-b, --batch          不进入交互式 sdb，直接连续运行
-d, --diff REF_SO    使用 REF_SO 开启 DiffTest
-p, --port PORT      DiffTest API 兼容参数，目前本地 REF 不使用
-l, --log FILE       设置 NPC_TRACE_LOG
--ftrace ELF         为 NPC ftrace 加载 ELF 符号
```

也可以直接运行：

```bash
./build/top path/to/img.bin
```

如果不传 `IMG`：

```bash
make run
```

程序会从空内存启动。这样可以进入 sdb，但没有有效程序可执行，通常只适合检查调试器是否能启动。

### 通过 AM 运行

AM 的 `riscv32e-npc` 平台脚本会生成 `.elf/.bin`，并调用 NPC：

```bash
export AM_HOME=/home/django/ysyx-workbench/abstract-machine
export NPC_HOME=/home/django/ysyx-workbench/npc
export NEMU_HOME=/home/django/ysyx-workbench/nemu

make -C ../am-kernels/tests/cpu-tests ARCH=riscv32e-npc ALL=dummy run-batch
make -C ../am-kernels/tests/cpu-tests ARCH=riscv32e-npc ALL=dummy difftest
```

其中 `run-batch` 会给 NPC 传 `-b`，`difftest` 会额外传 `NPC_DIFF=1`。当前环境使用 `riscv64-unknown-elf-` 作为默认 RISC-V 交叉编译器前缀；如需换工具链，可在命令行覆盖 `CROSS_COMPILE=...`。

### riscv32e-npc AM 对接

AM 侧的 NPC 平台公共逻辑在 [../abstract-machine/scripts/platform/npc.mk](../abstract-machine/scripts/platform/npc.mk)：

- `image`：把 AM 程序链接为 ELF，并通过 `objcopy` 生成 NPC 可加载的 `.bin`。
- `insert-arg`：把 `mainargs` 写入镜像中的占位字符串。
- `run`：执行 `$(MAKE) -C $(NPC_HOME) run ARGS="$(NPCFLAGS_BASE)" IMG=$(IMAGE).bin`，一键编译并运行 NPC。
- `run-batch`：在 `run` 基础上给 NPC 增加 `-b`，适合自动测试。
- `difftest`：在 `run-batch` 基础上设置 `NPC_DIFF=1`，让 NPC 自动链接 NEMU REF so。
- `NPCFLAGS_BASE`：默认传入 `-l build/.../npc-log.txt` 和 `--ftrace $(IMAGE).elf`，使 AM 运行时自动带上日志和 ELF 符号。

`riscv32e-npc` 的架构入口是 [../abstract-machine/scripts/riscv32e-npc.mk](../abstract-machine/scripts/riscv32e-npc.mk)，它复用 `scripts/isa/riscv.mk` 和 `scripts/platform/npc.mk`，并覆盖为 RV32E 编译参数：

```make
COMMON_CFLAGS += -march=rv32e_zicsr -mabi=ilp32e
LDFLAGS       += -melf32lriscv
```

AM 运行时退出协议在 [../abstract-machine/am/src/riscv/npc/trm.c](../abstract-machine/am/src/riscv/npc/trm.c)：

```c
void halt(int code) {
  asm volatile("mv a0, %0; ebreak" : : "r"(code));
  while (1);
}
```

也就是说，AM 程序的 `main()` 返回值会放入 `a0`，随后执行 `ebreak`。NPC 硬件在 `ExecuteStage` 识别 `ebreak` 后把 `a0` 输出为 `trap_code`；C++ 运行时把 `trap_code == 0` 视为 `HIT GOOD TRAP`，否则视为 `HIT BAD TRAP`。

### 本次 RV32E 指令扩展对应代码

主要硬件实现位于 [vsrc](vsrc) 下的手写 Verilog，`make all` 会直接把这些 RTL 文件交给 Verilator：

- [vsrc/minirv_defs.vh](vsrc/minirv_defs.vh)：定义 RV32E opcode/funct3 常量。
- [vsrc/npc_id_stage.v](vsrc/npc_id_stage.v)：生成 I/S/B/U/J 立即数并识别 `ebreak`。
- [vsrc/npc_ex_stage.v](vsrc/npc_ex_stage.v)：覆盖 RV32E 基础整数 ISA，并对非法 RV32E 编码产生 `trap_code = 2`。
- [vsrc/npc_mem_stage.v](vsrc/npc_mem_stage.v)：实现 `lb/lh/lw/lbu/lhu` 的写回扩展。
- [vsrc/npc_wb_stage.v](vsrc/npc_wb_stage.v)：实现 16 个 RV32E GPR 和 PC。

### 操作流程

1. 构建 NPC：

```bash
cd /home/django/ysyx-workbench/npc
make all
```

2. 通过 AM 编译并运行单个 cpu-test：

```bash
export AM_HOME=/home/django/ysyx-workbench/abstract-machine
export NPC_HOME=/home/django/ysyx-workbench/npc
export NEMU_HOME=/home/django/ysyx-workbench/nemu

make -C ../am-kernels/tests/cpu-tests ARCH=riscv32e-npc ALL=dummy run-batch
```

3. 运行一组覆盖计算、分支和访存的测试：

```bash
make -C ../am-kernels/tests/cpu-tests ARCH=riscv32e-npc ALL="add bit shift if-else load-store movsx" run-batch
```

4. 开启 DiffTest：

```bash
make -C ../am-kernels/tests/cpu-tests ARCH=riscv32e-npc ALL=dummy difftest
```

5. 手动运行已生成的镜像：

```bash
make run ARGS="-b" IMG=../am-kernels/tests/cpu-tests/build/dummy-riscv32e-npc.bin
```

### 为什么 `make run` 会输出这些内容

以：

```bash
make run IMG=/tmp/npc_sdb_test.bin
```

为例，当前实际输出类似：

```text
Loaded image: /tmp/npc_sdb_test.bin, size = 28 bytes
(npc)
```

含义如下：

- `Loaded image: ...` 来自 [npc_memory.cpp](csrc/npc_memory.cpp) 的 `load_img()`。它表示镜像文件已经读入 `pmem`，并打印文件路径和字节数。
- `(npc)` 来自 [npc_sdb.cpp](csrc/npc_sdb.cpp) 的 `sdb_mainloop()`。这表示 CPU 已经 reset 完成，正在等待你输入 sdb 命令。
- 如果执行 `c` 或单步到 `ebreak`，会出现 `HIT GOOD TRAP` 或 `HIT BAD TRAP`。这来自 trap 检查逻辑：`ebreak` 时硬件把 `a0` 作为 `trap_code`，`trap_code == 0` 表示 good trap，否则是 bad trap。
- 如果使用 `make run`，命令本身前面有 `@`，所以 Makefile 不会回显完整执行命令；你只会看到模拟器自己的输出。

## NVBoard GUI

默认不启用 NVBoard，因此普通构建不需要设置 `NVBOARD_HOME`。

启用 GUI：

```bash
export NVBOARD_HOME=/path/to/NVBoard
make nvboard
make NPC_GUI=1 run IMG=path/to/img.bin
```

`NPC_GUI=1` 会让 Makefile 编译 `auto_bind.cpp`，链接 NVBoard，并把运行时环境变量传给 `build/top`。

## 关键源码位置

- C++ 运行时： [csrc](csrc)
  - [csrc/main.cpp](csrc/main.cpp)
  - [csrc/npc_runtime.h](csrc/npc_runtime.h) / [csrc/npc_runtime.cpp](csrc/npc_runtime.cpp)
  - [csrc/npc_memory.h](csrc/npc_memory.h) / [csrc/npc_memory.cpp](csrc/npc_memory.cpp)
  - [csrc/npc_step.h](csrc/npc_step.h) / [csrc/npc_step.cpp](csrc/npc_step.cpp)
  - [csrc/npc_sdb.h](csrc/npc_sdb.h) / [csrc/npc_sdb.cpp](csrc/npc_sdb.cpp)
- Verilog 硬件： [vsrc](vsrc)
  - [vsrc/top.v](vsrc/top.v)
  - [vsrc/minirv_core.v](vsrc/minirv_core.v)
  - [vsrc/minirv_defs.vh](vsrc/minirv_defs.vh)
  - [vsrc/npc_if_stage.v](vsrc/npc_if_stage.v)
  - [vsrc/npc_id_stage.v](vsrc/npc_id_stage.v)
  - [vsrc/npc_ex_stage.v](vsrc/npc_ex_stage.v)
  - [vsrc/npc_mem_stage.v](vsrc/npc_mem_stage.v)
  - [vsrc/npc_wb_stage.v](vsrc/npc_wb_stage.v)

## 排查建议

- 如果 Verilator 报找不到 include 文件，先确认 [vsrc/minirv_defs.vh](vsrc/minirv_defs.vh) 仍在 `vsrc` 目录下。
- 如果程序访问 `x16`-`x31` 后立刻 bad trap，这是 RV32E 的预期行为；需要确认镜像使用 `-march=rv32e` 和 `-mabi=ilp32e` 编译。
- `make lint` 会直接对 [vsrc](vsrc) 下的手写 Verilog 执行 Verilator lint。
