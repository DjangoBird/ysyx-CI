# D4：RTL minirv NPC

## 学习记录

D4 从软件模拟器进入 RTL 处理器。核心不是一次实现完整 CPU，而是先把 IFU、IDU、EXU、
LSU、WBU 的基本边界划清楚，并通过 `ebreak` 让程序控制仿真结束。

## 实现记录

早期 minirv 已完成，当前已演进到 RV32E NPC。关键模块：

```text
npc/Makefile
npc/top.nxdc
npc/vsrc/top.v
npc/vsrc/minirv_core.v
npc/vsrc/npc_if_stage.v
npc/vsrc/npc_id_stage.v
npc/vsrc/npc_ex_stage.v
npc/vsrc/npc_mem_stage.v
npc/vsrc/npc_wb_stage.v
npc/csrc/main.cpp
npc/csrc/npc_runtime.cpp
npc/csrc/npc_memory.cpp
npc/csrc/npc_step.cpp
```

## 关键代码与讲解

### Verilator 顶层和构建链

NPC 的 Verilator 顶层是 `npc/vsrc/top.v`，Makefile 中固定：

```make
TOPNAME = top
VSRCS = $(shell find ./vsrc -name "*.v" -or -name "*.sv")
CSRCS = $(shell find ./csrc -name "*.c" -or -name "*.cc" -or -name "*.cpp")
BIN = $(BUILD_DIR)/$(TOPNAME)
```

含义：

- `vsrc` 中所有 Verilog 都交给 Verilator 转成 C++ 模型。
- `csrc` 中的 C++ 代码和 Verilator 模型一起编译成 `build/top`。
- `build/top` 不是硬件本身，而是“RTL 模型 + 仿真外壳”的可执行程序。

普通构建：

```sh
cd ~/ysyx-workbench/npc
make all
```

运行一个镜像：

```sh
make run ARGS="-b" IMG=../am-kernels/tests/cpu-tests/build/dummy-riscv32e-npc.bin
```

### `top.v`：把硬件和 C++ 外壳连起来

`top.v` 暴露几类端口：

```verilog
input  wire clk,
input  wire rst,
input  wire commit_ready,
output wire trap,
output wire [31:0] trap_code,
output wire commit_valid,
output wire [31:0] commit_pc,
output wire [31:0] commit_instr,
output wire [31:0] commit_next_pc,
output wire [31:0] dbg_x0_o ... dbg_x15_o
```

这些端口不是给真实芯片使用的全部接口，而是给 Verilator C++ 外壳、sdb、trace 和
DiffTest 观察架构状态：

- `commit_valid/commit_pc/commit_instr`：告诉 C++ 本周期提交了一条指令。
- `trap/trap_code`：告诉 C++ 程序结束或 bad trap。
- `dbg_x*`：sdb 和 DiffTest 读取寄存器。
- `commit_ready`：C++ 随机模式用来制造写回反压。

后续 B1 中 `top.v` 还连接了 AXI arbiter、Xbar、UART 和 CLINT；D4 阶段先理解它是
Verilator 和 RTL 的边界。

### `minirv_core.v`：把 IF/ID/EX/MEM/WB 串起来

当前核心按阶段组织：

```text
WBU 提供 pc/regfile
  -> IFU 根据 pc 取指
  -> IDU 译码
  -> EXU 执行和生成访存/跳转
  -> MEMU 处理 load/store
  -> WBU 写回 pc 和寄存器
```

关键点是 PC 和寄存器不散落在所有模块里，而是集中在 WBU：

```verilog
always @(posedge clk or posedge rst) begin
  if (rst) begin
    pc_reg <= 32'h8000_0000;
    regs[i] <= 32'b0;
  end else if (in_valid && in_ready) begin
    pc_reg <= pc_next;
    if (wb_en && wb_idx != 4'd0) regs[wb_idx] <= wb_data;
  end
end
```

这样做的意义：

- PC 复位地址和 AM 镜像加载地址一致。
- 只有提交时才更新架构状态。
- `x0` 永远不写。

### IFU/IDU/EXU/MEMU/WBU 各阶段职责

`npc_if_stage.v`：

- 输入 PC。
- 发起取指请求。
- 输出 `instr` 和 `pc_next_seq = pc + 4`。

`npc_id_stage.v`：

- 拆分 `opcode/rd/rs1/rs2/funct3/funct7`。
- 生成 I/S/B/U/J 立即数。
- 识别 `ebreak`。

`npc_ex_stage.v`：

- 执行 ALU、跳转、分支。
- 生成 load/store 地址、写掩码、写数据。
- 识别 system 指令。
- 对非法 RV32E 编码产生 trap。

`npc_mem_stage.v`：

- 对 load 数据做 `lb/lbu/lh/lhu/lw` 扩展。
- 对 store 等待写响应。

`npc_wb_stage.v`：

- 更新 PC。
- 写回 GPR。
- 暴露 debug 寄存器端口。

这个划分直接对应讲义中的模块化要求。后续查 bug 时也按这个边界定位，而不是把所有
信号都当成一团。

C++ 运行时根据 trap 结束仿真：

```cpp
if (dut.trap) {
  if (dut.trap_code == 0) {
    std::printf("HIT GOOD TRAP at pc = 0x%08x\n", dut.commit_pc);
    return 0;
  }
  std::printf("HIT BAD TRAP at pc = 0x%08x, code = %u\n",
              dut.commit_pc, dut.trap_code);
  return 1;
}
```

讲解：

- `ebreak` 不只是调试指令，在 AM 中也是程序退出协议。
- `a0 == 0` 表示 good trap。
- 这种方式比固定运行 N 个周期更稳定。

## 改动代码详解

### `main.cpp`：仿真程序入口

`main.cpp` 做的是仿真外壳初始化，不实现指令语义：

```text
解析 -b/-d/-l/--ftrace
  -> load_img()
  -> 可选 nvboard_init()
  -> npc_trace_init()
  -> reset()
  -> npc_difftest_init()
  -> sdb_mainloop() 或 run_simulation()
```

这条链路说明 NPC 的运行不是“直接执行 Verilog 文件”，而是先由 Verilator 生成
`Vtop` C++ 类，再由 `main.cpp` 创建和驱动这个模型。

### reset 不是只拉高信号，而是跑若干周期

```cpp
void reset(int n) {
  dut.rst = 1;
  while (n-- > 0) {
    (void)single_cycle();
  }
  dut.rst = 0;
}
```

RTL 中 PC、寄存器堆、总线状态机都是时序逻辑。reset 需要保持若干周期，让所有
寄存器进入确定状态。如果只在组合上拉一下 `rst`，有些状态可能没有在时钟边沿被
真正复位，后续表现为 PC 随机、trap 随机或访存状态异常。

### 运行循环只关心 trap

```cpp
int run_simulation() {
  reset(10);
  while (!Verilated::gotFinish()) {
    if (single_cycle()) break;
  }
  ...
}
```

模拟器不应该猜程序要跑多少周期。AM 程序结束时会执行 `ebreak`，硬件把它转换成
`trap`，C++ runtime 收到后停止。这种协议比“运行固定 N 周期”更可靠，也支持不同
程序长度。

### trap code 与 AM `halt(code)` 对齐

AM 的 `halt(code)` 最终把 `code` 放到 `a0`，然后执行 `ebreak`。NPC 在 `ebreak`
时把 `a0` 输出为 `trap_code`。因此：

- `trap_code == 0` 是 good trap。
- 非 0 是 bad trap。
- CPU 测试可以通过返回值表达成功或失败。

### Verilator 单周期推进

`npc_step.cpp` 中每次推进会改变 `clk` 并调用 `dut.eval()`：

```text
clk = 0; dut.eval()
  -> 组合逻辑稳定，C++ 观察握手和提交
clk = 1; dut.eval()
  -> 时序逻辑在上升沿更新
```

这和真实硬件的时钟边沿对应。调试时不要把 C++ 的一次循环误解成一条指令；后续
多周期和 AXI 等待会让一条指令跨多个 cycle。

### NVBoard 在 D4 阶段怎么看

NVBoard 不是必须路径，但它说明 RTL 顶层端口可以连接到图形化外设。当前绑定文件
是 `npc/top.nxdc`：

```text
top=top
led (LD7, LD6, LD5, LD4, LD3, LD2, LD1, LD0)
```

启用 GUI：

```sh
cd ~/ysyx-workbench/npc
export NVBOARD_HOME=/path/to/NVBoard
make nvboard
make NPC_GUI=1 run IMG=path/to/program.bin
```

构建时 Makefile 会用 `auto_pin_bind.py` 根据 `top.nxdc` 生成 `auto_bind.cpp`，然后
`main.cpp` 调用：

```cpp
nvboard_bind_all_pins(&dut);
nvboard_init();
```

运行过程中 `npc_step.cpp` 周期性调用 `nvboard_update()`。因此如果要在 NVBoard 上
观察新信号，必须先把内部信号接到 `top.v` 顶层端口，再在 `top.nxdc` 里绑定。

## 运行方式

```sh
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
make ARCH=riscv32e-npc ALL=dummy run-batch
```

Verilator/NVBoard 相关测试：

```sh
cd ~/ysyx-workbench/npc
make lint
make all
make run ARGS="-b" IMG=../am-kernels/tests/cpu-tests/build/dummy-riscv32e-npc.bin

# 有 NVBoard 环境时
make nvboard
make NPC_GUI=1 run IMG=../am-kernels/tests/cpu-tests/build/dummy-riscv32e-npc.bin
```

## Debug 心得

### 场景 1：Verilator 编译失败

先跑 lint：

```bash
cd ~/ysyx-workbench/npc
make lint
```

常见原因：

- 端口名在 `top.v` 和子模块实例化中不一致。
- 新增 Verilog 头文件被 `.gitignore` 忽略或没有 include。
- 位宽不匹配，Verilator 把 warning 当 error。
- `Vtop.h` 生成失败导致 C++ 编译找不到 DUT 类型。

排查顺序：

1. 先看第一条 Verilator 报错，不要从最后的 make error 看。
2. 如果是 include 文件找不到，检查 `npc/vsrc` 和 `.gitignore`。
3. 如果是端口连接错误，打开 `top.v` 和对应模块定义对照。
4. 如果是 C++ 找不到 `dut.xxx`，说明顶层端口名变了，C++ runtime 也要同步。

### 场景 2：PC 不动或第一条指令不对

检查链路：

```text
reset 是否释放
pc_reg 是否复位到 0x80000000
镜像是否加载到 pmem[0]
IFU 取指地址是否为 0x80000000
返回指令是否等于 bin 开头 4 字节
```

命令：

```bash
xxd -g 4 -l 16 path/to/program.bin
./build/top path/to/program.bin
(npc) si
(npc) info r
(npc) x 4 0x80000000
```

如果内存里第一条指令正确但 PC 不对，查 WBU reset；如果 PC 正确但 instr 不对，查
C++ `pmem_read32()` 和 IFU 地址。

### 场景 3：PC 能动但程序不结束

AM 程序退出依赖：

```text
halt(code)
  -> mv a0, code
  -> ebreak
  -> EXU 识别 ebreak
  -> top.v trap/trap_code
  -> C++ run_simulation 停止
```

排查：

1. `.txt` 中确认程序末尾确实有 `ebreak`。
2. itrace/NPC trace 确认 PC 执行到 `ebreak`。
3. 看 ID/EX 是否识别 `is_ebreak`。
4. 看 `trap_code` 是否等于 `a0`。
5. 看 C++ 是否在 `dut.trap` 后退出循环。

### 场景 4：trap code 错

trap code 对应 AM 返回值。排查：

```text
main 返回值
halt(code) 是否写 a0
WBU 中 x10/a0 是否正确
EXU ebreak 时读到的 a0_val 是否正确
top.v trap_code 是否接到 core_trap_code
```

如果 `a0` 在 sdb 中正确但 trap code 错，问题在 EXU/top 连接；如果 sdb 中 `a0` 已错，
回到产生返回值的指令排查。

### 场景 5：NVBoard 没反应

NVBoard 只观察顶层端口，不能直接看内部信号。排查：

```bash
export NVBOARD_HOME=/path/to/NVBoard
make nvboard
make NPC_GUI=1 run IMG=path/to/program.bin
```

检查：

1. `NPC_GUI=1` 是否传给 make run。
2. `NVBOARD_HOME` 是否正确。
3. `top.nxdc` 中端口名是否和 `top.v` 完全一致。
4. 是否重新生成了 `build/auto_bind.cpp`。
5. `main.cpp` 是否调用 `nvboard_bind_all_pins()` 和 `nvboard_init()`。
6. `npc_step.cpp` 是否周期性调用 `nvboard_update()`。

如果要观察新信号，先把内部信号接到 `top.v` 输出端口，再改 `top.nxdc`。

### 场景 6：Verilator gdb 看不到内部信号

C++ 侧稳定可看的信号是顶层端口：

```gdb
p/x dut.dbg_pc_o
p/x dut.dbg_x10_o
p dut.trap
p/x dut.trap_code
```

内部信号名由 Verilator 展开，可能随版本变化。长期调试依赖内部层级名不稳。需要长期
观察的信号，应该在 `top.v` 增加 debug 输出端口，或者开波形。
