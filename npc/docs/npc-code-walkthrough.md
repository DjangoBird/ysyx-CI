# NPC 代码走读：Verilog、C++、NVBoard 与 AM 串联

本文按当前仓库的实际运行路径说明 NPC。范围是：

- `npc/Makefile` 如何把 Verilog、C++、Capstone、DiffTest、NVBoard 串起来。
- `npc/vsrc` 中 CPU、AXI-Lite 总线、UART、CLINT 的连接关系。
- `npc/csrc` 中 Verilator 外壳、外部 SRAM 模型、sdb、trace、DiffTest 的工作方式。
- `abstract-machine` 如何把 AM 程序编译成 NPC 能加载的镜像，并启动 RT-Thread。

当前权威实现是 `npc/vsrc` + `npc/csrc`。`npc/src/main/scala` 是早期 Chisel 级间消息参考，不是当前 Verilator 和 STA 使用的顶层。

## 1. 总体数据流

从一条 AM 程序指令到 NPC 执行，大致链路是：

```text
AM/RT-Thread 源码
  -> abstract-machine Makefile 编译链接 ELF
  -> objcopy 生成 .bin
  -> npc/build/top 加载 .bin 到 C++ pmem
  -> Verilator 推进 top.v
  -> minirv_core 取指/译码/执行/访存/写回
  -> top.v 外部 AXI-Lite 口访问 C++ SRAM
  -> UART/CLINT 在 RTL 内完成 MMIO
  -> commit_valid 时 C++ 做 trace、DiffTest、监视点检查
```

硬件侧拓扑：

```text
                  +------------------+
                  |    minirv_core   |
                  |                  |
pc/reg/csr <------| WBU MEM EX ID IF |
                  +----+--------+----+
                       |        |
                    IFU AR/R   LSU AR/R/AW/W/B
                       |        |
                       +---+----+
                           |
                    npc_axi_arbiter
                           |
                       npc_axi_xbar
             +-------------+-------------+
             |             |             |
        external SRAM     UART          CLINT
        C++ pmem          RTL $write    RTL mtime
```

C++ 侧不是 CPU 功能实现，它只负责：

- 生成 Verilator 模型并推进时钟。
- 模拟外部 SRAM 的 AXI-Lite slave。
- 加载镜像、提供 sdb、trace、DiffTest、性能统计。
- 可选连接 NVBoard。

## 2. Makefile 串联方式

入口是 `npc/Makefile`。

关键变量：

- `TOPNAME = top`：Verilator 顶层固定为 `vsrc/top.v` 中的 `top`。
- `BUILD_DIR = build`：生成物在 `npc/build`。
- `OBJ_DIR = build/obj_dir`：Verilator 中间 C++ 和对象文件目录。
- `VSRCS`：自动收集 `vsrc/*.v` 和 `vsrc/*.sv`。
- `CSRCS`：自动收集 `csrc/*.c/*.cc/*.cpp`。
- `BIN = build/top`：最终可执行仿真器。

默认构建流程：

```text
make all
  -> verilator --cc --exe --build
  -> 读取 vsrc/top.v 及其子模块
  -> 编译 csrc/main.cpp、npc_step.cpp 等
  -> 链接 Capstone
  -> 生成 build/top
```

Capstone 用于 itrace 反汇编。Makefile 会加入：

```text
../nemu/tools/capstone/repo/include/capstone
../nemu/tools/capstone/repo/libcapstone.so.5
```

DiffTest 由 `NPC_DIFF=1` 控制。开启时 Makefile 会先构建 NEMU 共享库：

```text
../nemu/build/riscv32-nemu-interpreter-so
```

然后运行 NPC 时追加：

```text
-d ../nemu/build/riscv32-nemu-interpreter-so
```

NVBoard 由 `NPC_GUI=1` 控制。开启后 Makefile 会：

1. 使用 `nvboard/scripts/auto_pin_bind.py` 读取 `top.nxdc`。
2. 生成 `build/auto_bind.cpp`。
3. 编译 NVBoard C++ 源码。
4. 链接 SDL2 和 NVBoard。
5. 运行时通过环境变量 `NPC_GUI=1` 让 `main.cpp` 初始化 GUI。

`top.nxdc` 当前只绑定 LED：

```text
top=top
led (LD7, LD6, LD5, LD4, LD3, LD2, LD1, LD0)
```

所以 NVBoard 只显示 `top.v` 的 `led[7:0]`。当前 CPU 功能不依赖 GUI；GUI 只是观察外设端口。

## 3. Verilog 顶层 `top.v`

`top.v` 是 Verilator、NVBoard、STA 的统一顶层。

主要端口分四类：

- 时钟复位：`clk`、`rst`。
- 提交控制：`commit_ready`。C++ 随机模式会拉低它，用来测试写回反压。
- 外部 SRAM AXI-Lite：`mem_axi_ar*`、`mem_axi_r*`、`mem_axi_aw*`、`mem_axi_w*`、`mem_axi_b*`。
- 调试观察：`trap`、`trap_code`、`commit_*`、`dbg_x0_o` 到 `dbg_x15_o`。

顶层实例化：

- `minirv_core`：CPU 核心。
- `npc_axi_arbiter`：IFU 和 LSU 共享读通道仲裁，写通道 LSU 直通。
- `npc_axi_xbar`：按地址分发到 SRAM、UART、CLINT 或 error slave。
- `npc_axi_uart`：RTL 串口输出。
- `npc_axi_clint`：RTL 计时器。

`trap` 不是组合直出，而是在提交点锁存：

```text
commit_valid && core_trap
```

这样不会因为流水中间的无效消息或反压造成误判。`trap_code` 来自核心，AM 的 `halt(code)` 会把 `code` 放入 `a0` 后执行 `ebreak`，因此 `trap_code == 0` 是 good trap。

## 4. CPU 核心 `minirv_core.v`

`minirv_core.v` 负责把各级和 CSR 连起来。

当前是简单多周期结构，不是完整流水线。各级之间有 `valid/ready`，但同一时刻主要推进一条指令；访存等待时会反压前级。

模块连接：

- `npc_wb_stage` 保存 PC 和寄存器堆。
- `npc_if_stage` 按 PC 发起取指 AXI 读。
- `npc_id_stage` 解析指令字段和立即数。
- `npc_ex_stage` 执行 ALU、跳转、访存请求、CSR/system 指令。
- `npc_mem_stage` 完成 load/store AXI 事务和 load 数据扩展。
- `npc_csr_file` 保存 `mstatus/mtvec/mepc/mcause/mcycle` 等 CSR。

提交点定义：

```text
commit_valid = mem_valid && mem_ready
```

也就是 MEM 阶段输出被 WBU 接收的周期。C++ 的 trace 和 DiffTest 都以这个信号为准。

CSR 副作用也被握手门控：

- `ecall` 只有 EX/MEM 消息真正传递时写 `mepc/mcause`。
- access fault 只有 MEM/WB 响应完成并提交时写 CSR。
- 普通 CSR 写只在有效提交路径上发生。

这个设计避免了一个常见错误：下游还没 ready 时，上游组合逻辑已经多次触发 CSR 或 store 副作用。

## 5. 指令常量 `minirv_defs.vh`

`minirv_defs.vh` 集中定义 opcode 和 funct3：

- `OPCODE_OP`、`OPCODE_OP_IMM`、`OPCODE_LOAD`、`OPCODE_STORE`、`OPCODE_BRANCH`、`OPCODE_SYSTEM` 等。
- `F3_ADD_SUB`、`F3_SLL`、`F3_LB`、`F3_LHU`、`F3_SW` 等。

执行级和访存级都 include 它。`.gitignore` 已显式允许 `*.vh`，否则新克隆仓库可能缺这个头文件。

## 6. 取指级 `npc_if_stage.v`

IFU 是一个小状态机：

```text
REQUEST --AR fire--> RESPONSE --R fire--> OUTPUT --out fire--> REQUEST
```

各状态含义：

- `REQUEST`：拉高 `axi_arvalid`，地址为当前 PC。
- `RESPONSE`：等待 `axi_rvalid`，收到后锁存 `axi_rdata` 和 `axi_rresp`。
- `OUTPUT`：把锁存的指令通过级间 `out_valid/out_ready` 发给 IDU。

这里多一个 `OUTPUT` 状态很关键。它把“AXI R 响应握手”和“后级接收指令”拆开。否则共享总线下可能出现：

```text
IFU 持有读仲裁权
  -> R 数据组合直通后级
  -> 这条指令是 load，LSU 立刻请求总线
  -> LSU 等 IFU 释放仲裁器
  -> IFU 又等后级 ready 才释放 R
```

这会形成循环等待。现在 IFU 先锁存指令并释放 R 通道，再把指令交给后级。

如果 `rresp != OKAY`，IFU 产生 instruction access fault，后面 EX/CSR 会设置：

```text
mcause = 1
mepc   = faulting PC
pc     = mtvec
```

## 7. 译码级 `npc_id_stage.v`

IDU 是纯组合模块：

- 拆出 `opcode`、`rd_raw`、`rs1_raw`、`rs2_raw`、`funct3`、`funct7`。
- 生成 I/S/B/U/J 五类立即数。
- 将 RV32E 寄存器编号裁剪成低 4 位 `rd_idx/rs1_idx/rs2_idx`。
- 识别 `ebreak`。

RV32E 只有 `x0-x15`。是否访问非法寄存器不在 IDU 直接 trap，而是 EXU 根据 `rd_raw/rs*_raw < 16` 判断，这样可以统一处理 illegal instruction。

## 8. 执行级 `npc_ex_stage.v`

EXU 是当前 CPU 功能最多的模块。

它根据 opcode 完成：

- R 型整数运算：`add/sub/sll/slt/sltu/xor/srl/sra/or/and`。
- I 型整数运算：`addi/slti/sltiu/xori/ori/andi/slli/srli/srai`。
- `lui/auipc`。
- `jal/jalr`。
- 条件分支。
- load/store 请求生成。
- `fence/fence.i` 作为 no-op 顺序推进。
- `ecall/mret/ebreak/csrrw/csrrs`。

load/store 的地址在 EXU 计算，但真正访问总线在 MEMU：

- load 地址按 word 对齐传给 MEMU：`{load_addr[31:2], 2'b00}`。
- 原始低两位放到 `load_byte_off`，给 MEMU 做字节/半字选择。
- store 根据地址低位生成 `wmask` 和对齐后的 `wdata`。

异常和非法指令：

- 访问 `x16-x31`、未知 opcode、非法 funct3/funct7、未支持 SYSTEM 指令，会产生 illegal instruction trap。
- `trap_code = 2` 用于当前简化 illegal trap。
- `ebreak` 使用 `a0_val` 作为 `trap_code`。
- `ecall` 设置 `ecall=1`，下一 PC 为 `mtvec`。
- `mret` 下一 PC 为 `mepc`。

EXU 末尾有一段 `if (!in_valid)` 清零副作用信号。这是为了保证无效消息不会发出访存、CSR 写或 trap。

## 9. 访存级 `npc_mem_stage.v`

MEMU 处理数据 AXI-Lite 和 load 数据写回。

普通非访存指令：

```text
state == IDLE && !dmem_valid_in
  -> 直接 out_valid
```

load：

```text
IDLE --AR fire--> READ_RESPONSE --R fire--> IDLE
```

store：

```text
IDLE -> WRITE_REQUEST
WRITE_REQUEST: AW 和 W 分别握手，任意顺序
WRITE_RESPONSE: 等 B fire
```

AW/W 独立处理是 AXI-Lite 的关键点。代码中 `aw_done` 和 `w_done` 分别记录通道完成状态。某个通道握手后撤掉对应 valid，另一个通道继续保持载荷，直到两者都完成后才等待 B。

load 写回扩展也在 MEMU：

- `lb`：按 `load_byte_off` 取 8 位并符号扩展。
- `lbu`：取 8 位并零扩展。
- `lh`：取低半字或高半字并符号扩展。
- `lhu`：取低半字或高半字并零扩展。
- `lw`：返回完整 32 位。

错误响应：

- load `rresp != OKAY`：禁止写回，`mcause=5`。
- store `bresp != OKAY`：`mcause=7`。
- 下一 PC 改为 `mtvec`。

`commit_mem_valid` 是给 C++ trace/DiffTest 使用的提交观察口。它表示访存指令已经等到 R/B 响应并准备提交，不是请求刚发出的时刻。

## 10. 写回级 `npc_wb_stage.v`

WBU 保存体系结构 PC 和 16 个 RV32E GPR。

复位：

```text
pc = 0x80000000
x0-x15 = 0
```

只有在：

```text
in_valid && in_ready
```

成立时才：

- 更新 PC 为 `pc_next`。
- 如果 `wb_en && wb_idx != 0`，写寄存器。

`in_ready = commit_ready`。正常运行 C++ 让 `commit_ready=1`；随机 AXI 模式会随机拉低它，测试下游反压是否能传播到 MEMU、IFU 和总线响应通道。

调试端口 `dbg_x0_o` 到 `dbg_x15_o` 都来自 WBU 寄存器堆。sdb 和 DiffTest 不直接窥探 Verilator 内部层级，而是读取这些顶层端口。

## 11. CSR 文件 `npc_csr_file.v`

CSR 当前支持：

- `mcycle/mcycleh`
- `mvendorid`
- `marchid`
- `mstatus`
- `mtvec`
- `mepc`
- `mcause`

`mcycle` 每个 NPC 时钟周期加 1。注意它不是提交指令数，也不等于 NEMU REF 的执行步数。

异常写入优先级：

```text
普通 CSR 写
access_fault 写 mepc/mcause
ecall 写 mepc/mcause
```

代码实际是同一个 always 块中顺序执行，后面的 access fault/ecall 会覆盖同周期普通 CSR 对 `mepc/mcause` 的影响。异常路径保存的是触发异常的 PC，下一 PC 由 EX/MEM 路径改成 `mtvec`。

## 12. AXI 仲裁器 `npc_axi_arbiter.v`

CPU 内部有两个读 master：

- IFU：取指，只用 AR/R。
- LSU：load，用 AR/R；store 用 AW/W/B。

外部只有一套 SRAM/Xbar master 接口，所以需要仲裁。

读通道策略：

- 空闲时如果 LSU 有请求，优先 LSU。
- 否则选择 IFU。
- AR 握手时锁存 owner。
- R 握手完成后释放 owner。

写通道只有 LSU，所以 AW/W/B 直接转发。

当前没有 AXI ID，也不支持多个未完成读事务。仲裁器保证一次只放出一个读请求，因此返回的 R 一定能按锁存 owner 回到正确 master。

## 13. Xbar `npc_axi_xbar.v`

Xbar 做地址译码和权限控制：

| 地址 | 目标 | 访问 |
| --- | --- | --- |
| `0x80000000-0x87ffffff` | external SRAM | 读写 |
| `0xa0000048` | CLINT mtime low | 读 |
| `0xa000004c` | CLINT mtime high | 读 |
| `0xa00003f8` | UART TX | 写 |
| 其他 | error slave | 返回错误响应 |

读路径：

- AR 握手时锁存 `read_target`。
- SRAM/CLINT 返回正常 R。
- UART 或未知地址读返回 error R。

写路径：

- AW 阶段根据地址选择 `write_target`。
- W 阶段把数据转发给 SRAM 或 UART。
- CLINT 写和未知地址写返回 error B。

这个 Xbar 不是完整高性能互连。它牺牲了一些并发性，换来更容易验证的顺序状态机。对当前简单多周期 NPC 足够。

## 14. UART `npc_axi_uart.v`

UART 是 RTL AXI-Lite slave，不再由 C++ `pmem_write32()` 特判。

流程：

```text
AW fire
  -> address_done = 1
W fire
  -> 如果 wstrb[0]，$write("%c", wdata[7:0])
  -> response_valid = 1
B fire
  -> 清空状态
```

`$fflush()` 紧跟 `$write()`，因此 RT-Thread shell 的提示符不会卡在 Verilator stdout 缓冲里。

之前“最后一个 `msh />` 不输出”的根因可以归到写事务没有完整建模：如果 AW/W 任意一个被反压或丢失，UART 字符就会少。现在 UART、Xbar、MEMU 都按 AW/W/B 完整握手，store 收到 B 后才提交。

## 15. CLINT `npc_axi_clint.v`

CLINT 当前只实现 `mtime`：

- 64 位 `mtime`。
- reset 后从 0 开始。
- 每个 NPC 时钟周期加 1。
- 读 `0xa0000048` 返回低 32 位。
- 读 `0xa000004c` 返回高 32 位。

AM 的 `abstract-machine/am/src/riscv/npc/timer.c` 用 high-low-high 顺序读取：

```text
hi1 = mtime[1]
lo  = mtime[0]
hi2 = mtime[1]
如果 hi1 != hi2，重读
```

这样可以避免低 32 位进位时拼出错误的 64 位时间。

`abstract-machine/scripts/platform/npc.mk` 定义：

```text
NPC_CLOCK_FREQ=375000000
```

所以 AM 把 CLINT 周期按 375 MHz 换算成微秒。

## 16. C++ 入口 `main.cpp`

`main.cpp` 是 Verilator 可执行程序入口。

命令行参数：

- `-b/--batch`：批处理运行，不进入交互 sdb。
- `-d/--diff REF_SO`：开启 DiffTest。
- `-p/--port PORT`：DiffTest 兼容参数。
- `-l/--log FILE`：设置 trace 日志文件。
- `--ftrace ELF`：给 ftrace 加载 ELF 符号。

初始化顺序：

1. 解析参数。
2. 根据 `-l/--ftrace` 设置 `NPC_TRACE_LOG`、`NPC_FTRACE`、`NPC_FTRACE_ELF` 环境变量。
3. `Verilated::commandArgs()`。
4. `load_img()` 把镜像读入 `pmem`。
5. 如果编译并启用 NVBoard，调用 `nvboard_bind_all_pins()` 和 `nvboard_init()`。
6. `npc_trace_init()`。
7. `reset(10)`。
8. `npc_difftest_init()`。
9. 根据 batch 模式决定直接 `run_simulation()` 还是进入 `sdb_mainloop()`。

返回码：

- good trap 返回 0。
- bad trap 或仿真异常返回非 0。

## 17. 运行时全局对象 `npc_runtime.cpp`

这里定义三个核心全局对象：

- `Vtop dut`：Verilator 生成的顶层模型。
- `bool gui_enabled`：NVBoard 是否启用。
- `uint8_t pmem[PMEM_SIZE]`：外部 SRAM 字节数组。

其他 C++ 文件通过 `npc_runtime.h` 访问这些对象。

`npc_runtime.h` 也是 C++ 和 Verilator 模型的共同入口：

- `#include "Vtop.h"` 引入 Verilator 根据 `top.v` 生成的 C++ 类。
- `PMEM_BASE = 0x80000000` 和 `PMEM_SIZE = 128 MiB` 在这里统一定义。
- `extern Vtop dut` 保证所有 C++ 模块操作的是同一个 DUT。

调试 C++ 外壳时，先看是否通过 `dut.<top_port>` 访问顶层端口。当前实现刻意避免依赖
`dut.rootp->...` 这类内部层级名，因为 Verilator 版本或 RTL 层级变化后内部名很容易变。

## 18. C++ 模块接口头文件

`csrc/*.h` 的作用是固定各运行时模块的边界：

| 文件 | 暴露接口 | 作用 |
| --- | --- | --- |
| `npc_runtime.h` | `dut/gui_enabled/pmem/PMEM_BASE/PMEM_SIZE` | 全局 Verilator DUT、GUI 状态和物理内存 |
| `npc_memory.h` | `in_pmem/pmem_off/pmem_read32/pmem_write32/load_img` | 外部 SRAM 地址检查、读写和镜像加载 |
| `npc_step.h` | `update_mem_inputs/trap_hit/single_cycle/reset/run_simulation` | 时钟推进、复位和连续运行 |
| `npc_sdb.h` | `sdb_set_batch_mode/sdb_mainloop` | 批处理模式和交互调试器 |
| `npc_trace.h` | `npc_trace_init/close/get_current/commit` | trace 初始化和提交点记录 |
| `npc_difftest.h` | `npc_difftest_init/step/skip_ref/enabled` | REF 初始化、逐指令比较和 MMIO 跳过 |

这些接口形成一条清晰调用链：

```text
main.cpp
  -> load_img()
  -> npc_trace_init()
  -> reset()
  -> npc_difftest_init()
  -> sdb_mainloop() 或 run_simulation()
       -> single_cycle()
            -> npc_trace_commit()
            -> npc_difftest_step()/npc_difftest_skip_ref()
```

如果要加新调试功能，优先判断它属于提交点观察、交互命令、内存模型还是 DiffTest，
然后放到对应模块，避免把所有逻辑都塞进 `main.cpp`。

## 19. 外部 SRAM `npc_memory.cpp`

NPC 物理内存范围：

```text
PMEM_BASE = 0x80000000
PMEM_SIZE = 128 MiB
```

`load_img()` 把裸 `.bin` 直接读到 `pmem[0]`，所以镜像第一个字节对应客户机地址 `0x80000000`。

读写函数：

- `pmem_read32(addr)`：按小端拼出 32 位 word，越界返回 0。
- `pmem_write32(addr, data, wmask)`：按 `wmask` 写对应字节，越界忽略。

注意：这里现在只处理 SRAM。UART 和 CLINT 已在 RTL 中，C++ 内存模型不再识别 MMIO 地址。

## 20. 时钟推进和 AXI slave `npc_step.cpp`

`npc_step.cpp` 是 C++ 仿真外壳中最关键的文件。

它维护：

- 外部 SRAM 读 slave 状态。
- 外部 SRAM 写 slave 状态。
- AXI 随机 ready 和响应延迟。
- `valid && !ready` 期间载荷稳定性检查。
- commit 时 trace/DiffTest 调用。
- trap、性能统计和 NVBoard 更新。

单周期推进的大致顺序：

```text
begin_axi_cycle()
  -> 根据 slave 状态给 dut.mem_axi_*ready/*valid/*data
clk = 0; dut.eval()
  -> 观察本周期请求/响应是否 fire
  -> 检查 AR/AW/W 载荷稳定
  -> 捕获 commit_valid/pc/instr/mem/trap
clk = 1; dut.eval()
  -> RTL 状态更新
  -> C++ slave 消费握手、产生下一步响应
commit_valid?
  -> trace
  -> DiffTest
  -> watchpoint
trap?
  -> 打印统计
```

AXI 模式：

- `NPC_AXI_MODE=fixed`：ready 基本恒有效，响应无随机延迟，方便快速跑测试。
- `NPC_AXI_MODE=random`：随机 ready、R/B 延迟、WBU ready，用来压力测试握手。
- `NPC_AXI_SEED` 或 `NPC_BUS_SEED`：固定随机序列，方便复现。

为什么 C++ 要检查稳定性：

AXI 规定 master 在 `valid=1 && ready=0` 时不能改变地址、写数据或写掩码。随机模式会制造这种阻塞，如果 RTL 在阻塞期间改了载荷，C++ 立即报错。这比等程序跑飞更快定位总线协议问题。

DiffTest 跳过 MMIO：

REF NEMU 共享库没有 NPC 的 RTL UART/CLINT 设备。提交 MMIO load/store 时，DUT 可以正常执行，但 REF 不能直接执行同一条访存。因此 C++ 在这类提交后调用 `npc_difftest_skip_ref(next_pc)`，把 DUT 状态同步给 REF。

## 21. sdb `npc_sdb.cpp`

sdb 是 NPC 的交互调试器，默认启动。

寄存器读取：

- `npc_pc()` 读 `dut.dbg_pc_o`。
- `npc_reg(i)` 读 `dbg_x0_o` 到 `dbg_x15_o`。

支持命令：

- `c`：连续执行。
- `si [N]`：单步 N 条提交指令。
- `info r`：打印 PC 和 x0-x15。
- `info w`：打印监视点。
- `x N EXPR`：从表达式地址开始扫描内存。
- `p EXPR`：计算表达式。
- `w EXPR`：设置监视点。
- `d N`：删除监视点。
- `q`：退出。

表达式求值支持寄存器名、ABI 名、十六进制数、算术/逻辑/比较、括号和一元解引用。解引用最终调用 `pmem_read32()`，所以它只能直接读外部 SRAM，不读 RTL UART/CLINT。

## 22. trace `npc_trace.cpp`

NPC trace 以提交为边界，而不是以时钟周期为边界。

环境变量：

- `NPC_ITRACE=1`：打印提交指令 PC、机器码、反汇编。
- `NPC_MTRACE=1`：打印提交访存。
- `NPC_FTRACE=1`：打印 call/ret。
- `NPC_TRACE=1`：同时开启三者。
- `NPC_TRACE_LOG=path`：输出到文件。
- `NPC_FTRACE_ELF=path`：加载 ELF 符号表。

`npc_trace_commit()` 在 `commit_valid` 时调用。它拿到：

- `commit_pc`
- `commit_instr`
- `commit_next_pc`
- `commit_mem_*`

ftrace 通过 ELF 符号表解析函数名。对 `jal/jalr` call 和 `ret` 做简化识别；没有符号时退化成地址输出。

## 23. DiffTest `npc_difftest.cpp`

DiffTest 通过 `dlopen()` 加载 NEMU REF so，并解析：

- `difftest_memcpy`
- `difftest_regcpy`
- `difftest_exec`
- `difftest_raise_intr`
- `difftest_init`

NPC 的 DiffTest 状态结构是：

```text
gpr[16]
pc
```

这是 RV32E，不比较 x16-x31。

初始化：

1. 将 NPC `pmem` 拷贝到 REF。
2. 将 NPC 当前 GPR/PC 同步到 REF。
3. 开启 `difftest_on`。

每次普通提交：

```text
ref_difftest_exec(1)
ref_difftest_regcpy(..., TO_DUT)
读取 DUT 顶层 dbg_x*
比较 x0-x15 和 pc
```

如果不一致，打印：

```text
DiffTest: register mismatch after pc=...
DiffTest: pc mismatch after pc=...
```

定位时优先看 `after pc`，它表示刚提交的 DUT 指令 PC。

## 24. AM 与 NPC 平台脚本

AM 的 NPC 平台脚本是 `abstract-machine/scripts/platform/npc.mk`。

它做几件事：

- 收集 AM 源码：`start.S/trm.c/ioe.c/timer.c/input.c/cte.c/trap.S`。
- 设置链接地址：`_pmem_start=0x80000000`，入口 `_start`。
- `objdump` 生成 `.txt` 反汇编。
- `objcopy` 生成 `.bin`。
- 调用 `make -C $(NPC_HOME) run IMG=$(IMAGE).bin`。
- 默认传 `-l npc-log.txt` 和 `--ftrace $(IMAGE).elf`。

`run-batch` 会额外传 `-b`，适合自动测试。

`difftest` 会设置 `NPC_DIFF=1`，让 NPC Makefile 自动构建并使用 NEMU REF so。

RV32E 架构入口是 `abstract-machine/scripts/riscv32e-npc.mk`，它设置：

```text
-march=rv32e_zicsr
-mabi=ilp32e
```

如果程序访问 x16-x31，NPC 会按 RV32E 非法指令处理。因此编译参数必须和硬件 ISA 对齐。

AM 的 NPC 运行时源码在 `abstract-machine/am/src/riscv/npc`：

| 文件 | 作用 |
| --- | --- |
| `start.S` | 复位入口 `_start`，清 `s0`，设置 `sp = _stack_pointer`，调用 `_trm_init` |
| `trm.c` | 定义 heap、`putch()`、`halt()` 和 `_trm_init()` |
| `ioe.c` | AM IOE 分发表，注册 timer/input/uart config 等读写入口 |
| `timer.c` | 读取 RTL CLINT `mtime`，按 `NPC_CLOCK_FREQ` 转换为微秒 |
| `input.c` | 当前返回无按键，`AM_KEY_NONE` |
| `cte.c` | 设置 `mtvec`、分发异常事件、构造内核线程 Context、实现 `yield()` |
| `trap.S` | 保存/恢复寄存器和 CSR，调用 `__am_irq_handle()`，最后 `mret` |
| `mpe.c` | 当前单核占位实现 |
| `vme.c` | 当前无虚存占位实现 |
| `libgcc/*` | 给 RV32E 工具链补齐除法、64 位乘移位等运行时例程 |

启动链路：

```text
NPC reset PC = 0x80000000
  -> AM _start
  -> la sp, _stack_pointer
  -> call _trm_init
  -> 读取 mvendorid/marchid 并打印
  -> main(mainargs)
  -> halt(ret)
  -> mv a0, ret; ebreak
  -> NPC trap_code = a0
```

字符输出链路：

```text
putch(ch)
  -> *(volatile uint8_t *)0xa00003f8 = ch
  -> CPU store MMIO
  -> MEMU AW/W/B
  -> Xbar 路由到 UART
  -> npc_axi_uart.v $write + $fflush
```

异常和上下文切换链路：

```text
cte_init(handler)
  -> csrw mtvec, __am_asm_trap

yield()
  -> RV32E: li a5, -1; ecall
  -> EXU 识别 ecall，pc_next = mtvec，CSR 写 mepc/mcause
  -> __am_asm_trap 保存 x1/x3-x15 + mcause/mstatus/mepc
  -> __am_irq_handle()
       mcause=11 且 GPR1 == -1 -> EVENT_YIELD，mepc += 4
       否则 -> EVENT_SYSCALL 或 EVENT_ERROR
  -> 用户 handler 返回新的 Context
  -> trap.S 恢复寄存器和 CSR
  -> mret 回到 mepc
```

这里 `GPR1` 是 AM 的 Context 宏命名，不等于 RISC-V 的 x1。对 RV32E 来说，
`yield()` 用 `a5` 传 `-1`，对应 Context 中保存的寄存器槽位。RT-Thread 的线程切换
依赖 `kcontext()` 构造出的 `mepc/mstatus/a0`，以及 `trap.S` 返回时把新 Context
恢复到寄存器。

## 25. RT-Thread 如何跑起来

RT-Thread 的 abstract-machine BSP 最终也是走 AM 平台脚本：

```text
rt-thread-am/bsp/abstract-machine
  -> make ARCH=riscv32e-npc
  -> 使用 AM riscv32e-npc 平台
  -> 链接成 ELF/bin
  -> NPC 加载 bin 到 0x80000000
```

RT-Thread 运行依赖 NPC 的几条链路：

- `ecall/mret`：线程切换和异常返回。
- CSR：`mtvec/mepc/mcause/mstatus`。
- CLINT `mtime`：时间相关 API。
- UART：shell 输出。
- 访存 AXI：栈、全局变量、shell 缓冲。

看到：

```text
msh />
```

说明至少 UART store、B 响应提交、shell 主循环都已经走通。RT-Thread 是常驻 shell，输出提示符后不会自动退出。

## 26. NVBoard 串联细节

NVBoard 不是 CPU 必需路径。它的串联是：

```text
top.nxdc
  -> auto_pin_bind.py
  -> build/auto_bind.cpp
  -> main.cpp 调 nvboard_bind_all_pins(&dut)
  -> nvboard_init()
  -> npc_step.cpp 每周期 nvboard_update()
```

当前只绑定 LED：

```text
led[7:0] -> LD7..LD0
```

`top.v` 中 `led` 可以用于显示低位状态，例如当前实现可把调试状态或 trap 状态接到 LED。由于 `top.nxdc` 只绑定顶层端口，内部信号必须先通过 `top.v` 暴露出来，NVBoard 才能看见。

## 27. Debug 心得

### 27.1 先判断是硬件提交错，还是 C++ 外壳错

看 `commit_valid`。如果指令根本没有提交，问题在握手、总线响应或死锁；如果已经提交但状态错，问题多半在 EXU/MEMU/WBU 或指令语义。

### 27.2 load/store 错误先看三个点

1. EXU 算出的对齐地址、`wmask`、`wdata` 是否正确。
2. MEMU 是否等到 R/B 响应后才提交。
3. load 扩展是否按 `funct3` 和地址低位选择正确字节/半字。

对 `lh/lhu/lb/lbu`，不要只看最终 bad trap 的 `a0`。bad trap 前常会执行 `auipc a0, 0` 这类报错路径，导致 `a0` 看起来像某个 PC 附近地址。更可靠的是在出错 load 指令提交后立刻看寄存器。

### 27.3 UART 少字符优先怀疑 AW/W/B

UART 字符由 store MMIO 产生。少输出通常不是 shell 字符串问题，而是：

- AW 或 W 没有保持到 ready。
- Xbar 写目标没锁住。
- UART 没有保持 BVALID。
- MEMU 在 B 前提前提交或重复提交。

### 27.4 DiffTest 的 `after pc` 是关键

`DiffTest: ... after pc=0x...` 表示刚提交的 DUT 指令。先去 `.txt` 反汇编找这个 PC，再看这条指令经过 EXU、MEMU 还是 WBU。

MMIO 指令会 skip REF，所以 MMIO 附近的问题不能只靠 DiffTest。需要结合 mtrace、UART 输出和 AXI stall 统计。

### 27.5 随机 AXI 模式用于抓协议，不用于第一次定位语义

第一次定位指令语义错误，用 fixed 模式更清楚：

```bash
NPC_AXI_MODE=fixed
```

怀疑握手或反压问题，再切 random 并固定 seed：

```bash
NPC_AXI_MODE=random NPC_AXI_SEED=7
```

固定 seed 后，同一个协议 bug 能稳定复现，便于加波形或 gdb 断点。

### 27.6 AM/RT-Thread 异常问题按三层拆

如果 RT-Thread 卡在启动、线程切换或 shell 输入输出附近，按三层看：

1. 硬件层：`ecall` 是否提交，`mepc/mcause` 是否写入，`mret` 是否跳回正确地址。
2. AM 层：`trap.S` 保存的 Context 大小和 `riscv.h` 中 Context 结构是否一致。
3. 应用层：RT-Thread handler 是否返回了正确的新 Context，线程栈是否落在 PMEM 内。

这类问题不要一开始就怀疑 RT-Thread。先用 `etrace`、NPC `itrace` 和 sdb 的 `info r`
确认异常入口、返回地址、`sp`、`a0/a5` 是否符合预期。

### 27.7 文档和代码要一起验证

NPC 有 Verilog、C++、AM、NEMU REF、RT-Thread 多套代码。单看某一个目录容易误判。每次改动后至少跑：

```bash
cd ~/ysyx-workbench/npc
make lint
make test-access-fault

cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
NPC_AXI_MODE=random NPC_AXI_SEED=20260621 \
  make ARCH=riscv32e-npc ALL="add load-store movsx" difftest
```

RT-Thread 用：

```bash
cd ~/Templates/rt-thread-am/bsp/abstract-machine
NPC_AXI_MODE=random NPC_AXI_SEED=23 \
  make ARCH=riscv32e-npc run-batch
```

看到最终 `msh />` 后，说明预置命令已经执行完成；进程继续等待输入是正常现象。
