# D/C/B1 阶段实验报告

本文按一生一芯 v24.07 官网讲义的阶段顺序重构：每个模块都把“讲义要求、实现功能、
关键代码与讲解、运行方式、debug 过程/常见问题”放在同一节中，避免在文档中来回
跳转。

参考讲义：

- D1：<https://ysyx.oscc.cc/docs/2407/d/1.html>
- D2：<https://ysyx.oscc.cc/docs/2407/d/2.html>
- D3：<https://ysyx.oscc.cc/docs/2407/d/3.html>
- D4：<https://ysyx.oscc.cc/docs/2407/d/4.html>
- D5：<https://ysyx.oscc.cc/docs/2407/d/5.html>
- D6：<https://ysyx.oscc.cc/docs/2407/d/6.html>
- C1：<https://ysyx.oscc.cc/docs/2407/c/1.html>
- C2：<https://ysyx.oscc.cc/docs/2407/c/2.html>
- C3：<https://ysyx.oscc.cc/docs/2407/c/3.html>
- C4：<https://ysyx.oscc.cc/docs/2407/c/4.html>
- C5：<https://ysyx.oscc.cc/docs/2407/c/5.html>
- B1：<https://ysyx.oscc.cc/docs/2407/b/1.html>

## 0. 总体索引

**当前完成度**

| 阶段 | 当前状态 | 主要证据 |
| --- | --- | --- |
| D1 | 完成 RV32IM NEMU | `nemu/src/isa/riscv32/inst.c`，`dummy` PASS |
| D2 | 学习项，无编程任务 | AM 生成 ELF/bin/txt，itrace 可对照机器码 |
| D3 | 完成 AM/NEMU 运行链 | `platform/nemu.mk`，TRM，`ebreak` trap |
| D4 | 完成 minirv/NPC，后续演进到 RV32E | `npc/vsrc`、历史提交 `927751b/6e295a8` |
| D5 | 完成 NEMU IO，NPC UART/timer | `nemu/src/device`、`npc_axi_uart.v`、`npc_axi_clint.v` |
| D6 | SimpleBus 历史完成，当前演进到 AXI4-Lite | `npc/docs/b1-stage-bus-refactor.md` |
| C1 | 完成基础设施 | NEMU/NPC sdb、trace、DiffTest |
| C2 | 完成 RV32E NPC 和 AM 接入 | `riscv32e-npc`，NPC DiffTest |
| C3 | 学习项，沉淀 debug 方法 | `npc/docs/debug-history.md` |
| C4 | 学习项，实践 ELF/链接 | `.elf/.bin/.txt`、ftrace 符号 |
| C5 | 完成异常/CSR/RT-Thread | `pa4-stage1-rv32.md`、`c5-npc-rtthread.md` |
| B1 | 完成总线化、AXI-Lite、Access Fault、性能评估 | `b1-completion-checklist.md`、`test-access-fault` |

**主要提交**

```text
c4e4e3c NEMU RV32 基础指令
0ef557e AM/NEMU run-batch
defc96b NEMU trace/ftrace/mtrace
2c16d95 NEMU DiffTest checkregs
e81c6b2 NEMU/AM 设备 IO
927751b 早期 minirv/NPC
6e295a8 AM-NPC 运行链
55d3ac1 NPC 迁移到 Chisel RV32E 单周期
d9a43f4 RV32E 相关实现
e76b36c RV32 上下文切换
777ed00 NEMU etrace
66de2c2 NPC CSR/RT-Thread
a23de15/65ababd/ede3849 NPC 总线化和 SimpleBus/AXI-Lite 文档
```

查看方式：

```sh
git log --oneline --decorate --all -- nemu abstract-machine npc am-kernels
git show --stat c4e4e3c
git show --stat 66de2c2
git show --stat a23de15
```

## D1：支持 RV32IM 的 NEMU

### 讲义要求

D1 要求根据 PA2 阶段 1 实现支持 RV32IM 的 NEMU。核心模型是：

```text
while (1) {
  从 PC 指示的存储器位置取出指令;
  执行指令;
  更新 PC;
}
```

### 实现功能

当前 NEMU 已实现：

- RV32I 整数、访存、跳转、分支、系统指令。
- RV32M 乘除法扩展。
- `ecall/ebreak/mret/csrrw/csrrs`。
- `mstatus/mtvec/mepc/mcause/mvendorid/marchid`。
- AM 程序从 `0x80000000` 装载并运行到 `ebreak`。

核心文件：

```text
nemu/src/isa/riscv32/inst.c
nemu/src/isa/riscv32/system/intr.c
nemu/src/isa/riscv32/include/isa-def.h
nemu/src/cpu/cpu-exec.c
```

### 关键代码与讲解

取指和执行入口：

```c
int isa_exec_once(Decode *s) {
  s->isa.inst = inst_fetch(&s->snpc, 4);
  return decode_exec(s);
}
```

这段代码把 RV32 固定为 4 字节取指。`inst_fetch()` 会推进 `snpc`，然后
`decode_exec()` 根据指令编码更新寄存器、内存或 `dnpc`。

PC 更新在 CPU 执行层：

```c
static void exec_once(Decode *s, vaddr_t pc) {
  s->pc = pc;
  s->snpc = pc;
  isa_exec_once(s);
  cpu.pc = s->dnpc;
}
```

普通指令不改 `dnpc`，因此 `cpu.pc` 顺序前进；分支、跳转、异常会改写 `dnpc`。

CSR 和系统指令：

```c
#define csr_read(csr) ({ \
  word_t val = 0; \
  switch (csr) { \
    case 0x300: val = cpu.mstatus; break; \
    case 0x305: val = cpu.mtvec; break; \
    case 0x341: val = cpu.mepc; break; \
    case 0x342: val = cpu.mcause; break; \
    case 0xf11: val = 0x79737978; break; \
    case 0xf12: val = 22040000; break; \
    default: panic("unsupported csr = 0x%x", (uint32_t)(csr)); \
  } \
  val; \
})
```

这里把只读 ID CSR 直接做成常量，避免为只读信息增加 CPU 状态。`mstatus/mtvec/mepc`
和 `mcause` 保存在 `CPU_state` 中，因为它们会被 CTE 和 RT-Thread 读写。

```c
INSTPAT("0000000 00000 00000 000 00000 11100 11", ecall,
    N, s->dnpc = isa_raise_intr(11, s->pc));
INSTPAT("0011000 00010 00000 000 00000 11100 11", mret,
    N, s->dnpc = cpu.mepc);
INSTPAT("0000000 00001 00000 000 00000 11100 11", ebreak,
    N, NEMUTRAP(s->pc, R(10)));
```

讲解：

- `ecall` 保存异常现场并跳转到 `mtvec`。
- `mret` 从 `mepc` 返回。
- `ebreak` 在 AM 中作为程序结束标志，`a0` 是退出码。

异常入口：

```c
word_t isa_raise_intr(word_t NO, vaddr_t epc) {
  IFDEF(CONFIG_ETRACE, {
    log_write("etrace cause=" FMT_WORD " (%s) epc=" FMT_WORD
        " target=" FMT_WORD "\n", NO, exception_name(NO), epc, cpu.mtvec);
  });

  cpu.mepc = epc;
  cpu.mcause = NO;
  return cpu.mtvec;
}
```

这段代码把异常处理固定为三步：记录 etrace、保存 `mepc/mcause`、返回 `mtvec`。
这样 NEMU 侧能非侵入地观察异常，不需要在客户程序里插 `printf()`。

### 运行方式

NEMU 配置必须是 native ELF，不是 DiffTest REF 的 shared object：

```sh
cd ~/ysyx-workbench/nemu
rg -n "CONFIG_TARGET|CONFIG_RVE|CONFIG_DEVICE|CONFIG_TRACE" .config include/generated/autoconf.h
make ISA=riscv32
```

最小验证：

```sh
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
make ARCH=riscv32-nemu ALL=dummy run-batch
```

本次复测：

```text
nemu: HIT GOOD TRAP at pc = 0x80000030
[         dummy] PASS
```

### Debug 过程和问题

1. 如果 AM 命令段错误并显示执行 `riscv32-nemu-interpreter-so`，说明 NEMU 配置停在
   `CONFIG_TARGET_SHARE=y`。需要用 `make ISA=riscv32 menuconfig` 切回 native ELF。
2. 如果 `run` 停在 `(nemu)`，这是交互模式，需要输入 `c`；自动运行使用 `run-batch`。
3. 如果 `etrace` 没输出，检查 `CONFIG_TRACE/CONFIG_ETRACE` 和 `TRACE_END`，并确认程序
   走的是 `ecall` 等异常路径。

## D2：程序的机器级表示

### 讲义要求

D2 是学习 C 程序到机器级表示的转换，官网没有新增编程任务。

### 实现功能

AM 构建会生成：

```text
program.elf
program.bin
program.txt
```

其中 `.txt` 是反汇编文件，能和 NEMU/NPC 的 itrace 对照。

### 关键代码与讲解

AM 平台脚本生成反汇编：

```make
image: image-dep
	@$(OBJDUMP) -d $(IMAGE).elf > $(IMAGE).txt
	@$(OBJCOPY) -S --set-section-flags .bss=alloc,contents \
	  -O binary $(IMAGE).elf $(IMAGE).bin
```

`.elf` 保留符号和段信息，`.bin` 是实际加载到模拟器的镜像，`.txt` 供调试阅读。

### 运行方式

```sh
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
make ARCH=riscv32-nemu ALL=dummy run-batch
less build/dummy-riscv32-nemu.txt
```

### Debug 过程和问题

当执行流跑飞时，先用 `.txt` 找到目标 PC 附近的指令，再对照 `nemu-log.txt` 中的
itrace。这样能判断是译码错误、跳转目标错误，还是内存镜像加载错误。

## D3：运行时环境与 AM

### 讲义要求

D3 要求理解 PA2 中程序、运行时环境和 AM 的关系，让 NEMU 能运行 AM 程序。

### 实现功能

- AM 镜像链接到 `0x80000000`。
- NEMU 把 bin 加载到 `RESET_VECTOR`。
- TRM 调用 `main()`，返回后通过 `halt()` 触发 `ebreak`。
- `run-batch` 支持自动运行，不进入 sdb 交互。

### 关键代码与讲解

`abstract-machine/scripts/platform/nemu.mk`：

```make
LDFLAGS += --defsym=_pmem_start=0x80000000 --defsym=_entry_offset=0x0
NEMUFLAGS_BASE += -l $(shell dirname $(IMAGE).elf)/nemu-log.txt
NEMUFLAGS_BASE += --ftrace $(IMAGE).elf
```

讲解：

- `_pmem_start=0x80000000` 对齐 NEMU 物理内存起点。
- `-l build/nemu-log.txt` 固定日志位置。
- `--ftrace $(IMAGE).elf` 让 NEMU 能解析函数符号。

### 运行方式

```sh
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
make ARCH=riscv32-nemu ALL=dummy run-batch
make ARCH=riscv32-nemu ALL="add load-store div" run-batch
```

### Debug 过程和问题

如果程序没有从预期位置开始，检查三点：

1. ELF 链接地址是否是 `0x80000000`。
2. NEMU `RESET_VECTOR` 是否一致。
3. `load_img()` 是否把 bin 写到 `guest_to_host(RESET_VECTOR)`。

## D4：RTL minirv NPC

### 讲义要求

D4 要求用 RTL 实现迷你 RISC-V 处理器：

- 模块化 IFU/IDU/EXU/LSU/WBU。
- 先实现 `addi` 和 `jalr`。
- 用仿真环境识别 `ebreak` 结束程序。
- 支持 minirv 指令并运行 AM `dummy`。

### 实现功能

当前早期 minirv 已经演进为 RV32E NPC，但仍保留分级结构：

```text
npc/vsrc/npc_if_stage.v
npc/vsrc/npc_id_stage.v
npc/vsrc/npc_ex_stage.v
npc/vsrc/npc_mem_stage.v
npc/vsrc/npc_wb_stage.v
```

### 关键代码与讲解

当前 `ebreak` 在 ID/EX 中识别，最终通过顶层 trap 端口通知 C++ 运行时。C++ 根据
`trap_code == 0` 判断 good trap。

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

- 程序自己通过 `ebreak` 告诉仿真器结束，而不是仿真器猜测运行多少周期。
- `trap_code` 来自 `a0`，与 AM `halt(code)` 语义一致。

### 运行方式

当前使用 RV32E NPC：

```sh
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
make ARCH=riscv32e-npc ALL=dummy run-batch
```

### Debug 过程和问题

早期 NPC 容易出现“PC 能动但程序不结束”的问题。排查顺序：

1. `ebreak` 是否被译码识别。
2. `a0` 是否作为 trap code 输出。
3. C++ runtime 是否在 trap 后停止仿真。
4. PC 复位地址是否为 `0x80000000`。

## D5：设备和输入输出

### 讲义要求

D5 要求：

- NEMU 完成 PA2 阶段 3 的输入输出。
- NPC 增加串口和时钟。
- 可以运行 timer、device 类 AM 测试。

### 实现功能

NEMU：

- `nemu/src/device/io` 实现 MMIO 映射。
- serial、rtc、keyboard、vga、audio、disk 注册为设备。
- `paddr_read/write()` 将非 pmem 地址转发到 MMIO。

NPC：

- D/C 阶段由 C++ 环境处理串口和 timer。
- 当前 B1 已把 UART/CLINT 迁移为 RTL AXI-Lite slave。

### 关键代码与讲解

NEMU 物理内存访问：

```c
word_t paddr_read(paddr_t addr, int len) {
  if (likely(in_pmem(addr))) {
    word_t ret = pmem_read(addr, len);
    IFDEF(CONFIG_MTRACE, if (MTRACE_COND) {
      log_write("mtrace R addr=" FMT_PADDR " len=%d data=" FMT_WORD
          " pc=" FMT_WORD "\n", addr, len, ret, cpu.pc);
    });
    return ret;
  }
  IFDEF(CONFIG_DEVICE, return mmio_read(addr, len));
  out_of_bound(addr);
  return 0;
}
```

讲解：

- pmem 内地址走普通内存。
- 非 pmem 地址在 `CONFIG_DEVICE` 开启时转到设备。
- mtrace 放在 pmem 访问路径中，用于定位错误读写。

### 运行方式

```sh
cd ~/ysyx-workbench/am-kernels/tests/am-tests
make ARCH=riscv32-nemu mainargs=t run-batch
make ARCH=riscv32-nemu mainargs=d run
make ARCH=riscv32-nemu mainargs=y run
```

NPC timer：

```sh
cd ~/ysyx-workbench/am-kernels/tests/am-tests
make ARCH=riscv32e-npc mainargs=t run-batch
```

### Debug 过程和问题

如果设备测试失败：

1. 确认 `CONFIG_DEVICE=y`。
2. 启动日志应打印 serial、rtc、keyboard 等 MMIO map。
3. 对串口问题，先确认写地址是否是 `0xa00003f8`。
4. 对 timer 问题，确认低/高 32 位读取顺序，避免 64 位计数撕裂。

## D6：D 阶段流片准备

### 讲义要求

D6 包含：

- 学习总线并实现支持有效信号的 SimpleBus。
- 接入 ysyxSoC，Flash 从 `0x30000000` 启动。
- 实现 `flash_read()` 并运行 hello/dummy。

### 实现功能

当前仓库历史中已完成 SimpleBus 重构，后来继续演进到 AXI4-Lite。当前主线 NPC
顶层已经不是 D6 讲义中的 ysyxSoC SimpleBus 接口，而是 B1 阶段单一 SRAM
AXI4-Lite 接口。

### 关键代码与讲解

D6 的 SimpleBus 思路在 B1 中被 AXI-Lite 覆盖。当前报告将总线实现集中放在 B1
模块中说明。

### 运行方式

SimpleBus 历史验证见：

```text
npc/docs/b1-stage-bus-refactor.md
```

### Debug 过程和问题

当前报告不把 ysyxSoC 接入声明为已验证。如果要做 D 阶段流片验收，需要单独复核：

1. 顶层接口是否匹配 SoC wrapper。
2. PC 是否从 Flash 地址取指。
3. `flash_read()` 是否返回正确字节序。
4. hello/dummy 是否在 SoC 环境运行。

## C1：工具和基础设施

### 讲义要求

C1 要求根据 PA2 阶段 2 搭建基础设施，为后续 NPC 调试和验收服务。

### 实现功能

NEMU：

- sdb
- itrace/mtrace/ftrace/etrace
- DiffTest
- watchpoint

NPC：

- sdb
- itrace/mtrace/ftrace
- DiffTest
- 随机总线反压

### 关键代码与讲解

NEMU 的 trace、DiffTest、watchpoint 都统一在指令完成后处理：

```c
static void trace_and_difftest(Decode *_this, vaddr_t dnpc) {
#ifdef CONFIG_ITRACE_COND
  if (ITRACE_COND) { log_write("%s\n", _this->logbuf); }
#endif
#ifdef CONFIG_ITRACE
  iringbuf_record(_this->logbuf);
#endif
  if (g_print_step) { IFDEF(CONFIG_ITRACE, puts(_this->logbuf)); }
  IFDEF(CONFIG_DIFFTEST, difftest_step(_this->pc, dnpc));
  IFDEF(CONFIG_WATCHPOINT, check_watchpoints());
}
```

讲解：

- trace 记录的是“指令执行后的事实”。
- DiffTest 在同一边界推进 REF。
- watchpoint 也在指令后检查，避免看到半更新状态。

### 运行方式

开启 NEMU trace：

```sh
cd ~/ysyx-workbench/nemu
make ISA=riscv32 menuconfig
```

NPC trace：

```sh
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
NPC_TRACE=1 make ARCH=riscv32e-npc ALL=dummy run-batch
```

### Debug 过程和问题

基础设施调试经验：

1. 先用 itrace 看 PC 是否按预期推进。
2. 再用 mtrace 看访存地址和数据。
3. 函数调用错乱时用 ftrace。
4. 异常错乱时用 etrace。
5. NPC 多周期后只看提交点，不看任意周期。

## C2：支持 RV32E 的单周期 NPC

### 讲义要求

C2 要求：

- NPC 搭建 sdb。
- NPC 添加 itrace/mtrace/ftrace。
- NPC 添加 DiffTest，REF 选择 NEMU shared object。
- AM 支持 `riscv32e-npc`。
- 实现 RV32E 指令集。

### 实现功能

核心文件：

```text
npc/csrc/npc_sdb.cpp
npc/csrc/npc_trace.cpp
npc/csrc/npc_difftest.cpp
abstract-machine/scripts/riscv32e-npc.mk
abstract-machine/scripts/platform/npc.mk
npc/vsrc/npc_ex_stage.v
```

已支持 RV32E 基础整数、访存、跳转、分支、`fence/fence.i`、`ebreak/ecall/mret` 和
CSR 指令。RV32E 只包含 `x0`-`x15`，访问 `x16`-`x31` 会被视作非法指令。

### 关键代码与讲解

NPC DiffTest 在提交点推进：

```cpp
if (!dut.rst && current_commit_valid) {
  ++instruction_count;
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

- RV32E NPC 后续变成多周期后，不能按 cycle 推进 REF。
- `commit_valid` 才表示架构状态真的改变。
- MMIO 访问无法在无设备 NEMU REF 中执行，因此使用 `skip_ref` 同步状态。

### 运行方式

```sh
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
make ARCH=riscv32e-npc ALL=dummy run-batch
make ARCH=riscv32e-npc ALL="add bit shift if-else load-store movsx" run-batch
make ARCH=riscv32e-npc ALL="add load-store movsx" difftest
```

### 讲义问题和回答

有符号加法和无符号加法硬件是否不同？

答：加法器本身不区分 signed/unsigned，`int32_t` 和 `uint32_t` 加法通常都使用同一条
`add`。差异主要出现在比较、除法、扩展等操作。

RV32E 没有乘除指令，C 程序为什么还能写乘除法？

答：编译器会调用 libgcc 软件函数，例如 `__mulsi3()`，用基础指令模拟乘除法。

移位会综合成什么？

答：变量移位通常综合成 barrel shifter，多级 mux 选择不同移位量；常量移位通常是
连线重排。

### Debug 过程和问题

NPC DiffTest 常见误区：

1. trap 指令本身 PC 行为和 NEMU 可能不同，不能直接做普通指令对比。
2. MMIO 指令不能让无设备 REF 原样执行。
3. RV32E 程序必须用 `ARCH=riscv32e-npc`，不能把 RV32I/M 镜像直接给 RV32E NPC。

## C3：调试技巧

### 讲义要求

C3 是调试方法学习，没有新增编程任务。

### 实现功能

当前 debug 体系：

- 应用层输出：看 AM/RT-Thread 行为。
- 函数层：ftrace。
- 指令层：itrace。
- 访存层：mtrace。
- 异常层：etrace。
- 硬件层：NPC trace、Verilator 波形。
- 协议层：随机 AXI 反压和 stability check。

### 关键代码与讲解

NPC 中检查 AXI payload 稳定：

```cpp
static void check_stability(RequestMonitor *monitor, const char *name,
                            bool valid, bool ready, uint32_t addr,
                            uint32_t data, uint8_t strb) {
  if (valid && monitor->stalled &&
      (monitor->addr != addr || monitor->data != data ||
       monitor->strb != strb)) {
    std::fprintf(stderr, "%s changed while VALID was stalled\n", name);
    std::exit(1);
  }
  if (valid && !ready) {
    monitor->stalled = true;
    monitor->addr = addr;
    monitor->data = data;
    monitor->strb = strb;
  } else {
    monitor->stalled = false;
  }
}
```

讲解：

- `valid && !ready` 时 payload 必须保持不变。
- 这个检查能抓住随机反压下才出现的总线协议 bug。

### 运行方式

```sh
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
NPC_AXI_MODE=random NPC_AXI_SEED=7 \
  make ARCH=riscv32e-npc ALL="dummy add bit shift if-else load-store movsx" run-batch
```

### Debug 过程和问题

历史 debug 经验见：

```text
npc/docs/debug-history.md
```

重点经验：

- 先看提交边界，再看周期波形。
- 少输出字符优先怀疑 MMIO store 或 B 响应。
- load 死锁优先检查 IFU R 响应是否被后级反压间接阻塞。

## C4：ELF 文件和链接

### 讲义要求

C4 是 ELF 和链接学习，没有新增编程任务。

### 实现功能

当前 AM 构建保留 ELF 并传给 ftrace：

```make
NEMUFLAGS_BASE += --ftrace $(IMAGE).elf
NPCFLAGS_BASE += --ftrace $(IMAGE).elf
```

### 关键代码与讲解

ftrace 读取 ELF 符号表，筛出函数符号，再在执行 `jal/jalr/ret` 时输出调用关系。

讲解：

- `.bin` 用于加载执行。
- `.elf` 用于符号解析。
- `.txt` 用于人读反汇编。

### 运行方式

```sh
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
make ARCH=riscv32-nemu ALL=dummy run-batch
readelf -S build/dummy-riscv32-nemu.elf
readelf -s build/dummy-riscv32-nemu.elf
```

### Debug 过程和问题

如果 ftrace 只有地址没有函数名，通常是没有传 ELF 或 ELF 符号被裁掉。先确认 AM
脚本中的 `--ftrace $(IMAGE).elf` 是否生效。

## C5：异常处理和 RT-Thread

### 讲义要求

C5 要求：

- NEMU 完成 PA3.1 自陷。
- NEMU 根据 PA4.1 启动 RT-Thread。
- NPC 添加 `mcycle/mcycleh/mvendorid/marchid` 等 CSR。
- NPC 实现 `ecall/mret`。
- NPC 启动 RT-Thread，并处理最后 `msh />` 输出问题。

### 实现功能

NEMU：

- `mstatus/mtvec/mepc/mcause`。
- `ecall/mret/csrrw/csrrs`。
- AM CTE、Context、`kcontext()`。
- RT-Thread 启动。

NPC：

- CSR 文件。
- `mcycle/mcycleh/mvendorid/marchid`。
- `ecall/mret`。
- RV32E CTE 和 Context。
- RT-Thread shell。

### 关键代码与讲解

AM CTE 识别 yield：

```c
switch (c->mcause) {
  case 11:
    if (c->GPR1 == (uintptr_t)-1) {
      ev.event = EVENT_YIELD;
      c->mepc += 4;
    } else {
      ev.event = EVENT_SYSCALL;
    }
    break;
  default: ev.event = EVENT_ERROR; break;
}

c = user_handler(ev, c);
assert(c != NULL);
return c;
```

`trap.S` 真正切换 Context：

```asm
mv a0, sp
call __am_irq_handle
mv sp, a0

LOAD t1, OFFSET_STATUS(sp)
LOAD t2, OFFSET_EPC(sp)
csrw mstatus, t1
csrw mepc, t2

MAP(REGS, POP)
addi sp, sp, CONTEXT_SIZE
mret
```

讲解：

- `__am_irq_handle()` 返回下一个 Context。
- `mv sp, a0` 把恢复源切到新线程。
- 所有寄存器恢复都从新 Context 读取。
- `mepc += 4` 防止 `mret` 后再次执行同一条 `ecall`。

NPC CSR：

```verilog
if (access_fault) begin
  mepc_reg <= access_fault_pc;
  mcause <= access_fault_cause;
end else if (ecall) begin
  mepc_reg <= ecall_pc;
  mcause <= 32'd11;
end
```

讲解：

- Access Fault 和 ecall 统一写 `mepc/mcause`。
- Access Fault 优先级更高，避免总线错误被 ecall 覆盖。

### 运行方式

NEMU yield-os：

```sh
cd ~/ysyx-workbench/am-kernels/kernels/yield-os
make ARCH=riscv32-nemu run-batch
```

NPC CSR ID：

```sh
cd ~/ysyx-workbench/am-kernels/kernels/hello
make ARCH=riscv32e-npc run-batch
```

NPC RT-Thread：

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

### Debug 过程和问题

RT-Thread 常见问题：

1. 没有进入异常：看 etrace 或 CSR 写入。
2. yield 后死循环：检查 `mepc += 4`。
3. 上下文没有切换：检查 `trap.S` 是否 `mv sp, a0`。
4. 最后 `msh />` 不输出：检查 UART store 是否等到 B 响应提交。

## B1：总线、AXI-Lite、Access Fault 和性能

### 讲义要求

B1 要求：

- 用 `valid/ready` 重构处理器内部通信。
- IFU/LSU 访存支持请求/响应和完整握手。
- 从 SimpleBus 演进到 AXI4-Lite。
- IFU/LSU 通过仲裁器共享存储器接口。
- 通过 Xbar 接入 SRAM、UART、CLINT。
- 总线错误转换为 Access Fault。
- 随机反压验证协议。
- 评估总线化前后的 IPC/Fmax/面积。

### 实现功能

当前 B1 权威实现位于 `npc/vsrc` 和 `npc/csrc`。Chisel 目录保留早期参考。

结构：

```text
IFU --+
      +--> AXI4-Lite arbiter --> Xbar --> external SRAM
LSU --+                              +-> UART  0xa00003f8
                                      +-> CLINT 0xa0000048/0xa000004c
                                      +-> DECERR
```

核心文件：

```text
npc/vsrc/minirv_core.v
npc/vsrc/npc_if_stage.v
npc/vsrc/npc_mem_stage.v
npc/vsrc/npc_axi_arbiter.v
npc/vsrc/npc_axi_xbar.v
npc/vsrc/npc_axi_uart.v
npc/vsrc/npc_axi_clint.v
npc/vsrc/npc_csr_file.v
npc/csrc/npc_step.cpp
```

### 关键代码与讲解

**IFU 响应缓冲**

```verilog
STATE_RESPONSE: begin
  if (axi_rvalid && axi_rready) begin
    instr_reg <= axi_rdata;
    access_fault_reg <= axi_rresp != 2'b00;
    state <= STATE_OUTPUT;
  end
end
```

```verilog
assign axi_rready = (state == STATE_RESPONSE);
assign out_valid = (state == STATE_OUTPUT);
assign instr = instr_reg;
```

讲解：

- IFU 先接收并锁存 R 响应，再向后级输出。
- 这样可以释放 AXI 仲裁器，避免 IFU R 响应被后级 load 间接阻塞。
- `axi_rresp` 同时锁存，取指错误不会被丢失。

**LSU AW/W/B 独立握手**

```verilog
STATE_WRITE_REQUEST: begin
  if (aw_fire) aw_done <= 1'b1;
  if (w_fire) w_done <= 1'b1;
  if ((aw_done || aw_fire) && (w_done || w_fire)) begin
    state <= STATE_WRITE_RESPONSE;
  end
end
STATE_WRITE_RESPONSE: begin
  if (axi_bvalid && axi_bready) state <= STATE_IDLE;
end
```

讲解：

- AW 和 W 哪个先到都可以。
- 已经握手的通道撤销 `valid`，避免重复发送。
- 两条通道都完成后才等待 B。
- store 的提交点是 B 握手，不是 AW/W 握手。

**Access Fault**

```verilog
wire load_access_fault = state == STATE_READ_RESPONSE &&
                         axi_rvalid && axi_rresp != 2'b00;
wire store_access_fault = state == STATE_WRITE_RESPONSE &&
                          axi_bvalid && axi_bresp != 2'b00;
assign access_fault = in_valid &&
                      (load_access_fault || store_access_fault);
assign access_fault_cause = load_access_fault ? 32'd5 : 32'd7;
assign wb_en_out = access_fault ? 1'b0 : wb_en_in;
assign pc_next_out = access_fault ? mtvec : pc_next_in;
```

讲解：

- 错误必须在响应完成点判断。
- load fault 禁止写回错误数据。
- store fault 等 B 响应后产生。
- 下一 PC 指向 `mtvec`，软件可在异常处理后 `mret`。

**UART**

```verilog
if (wvalid && wready) begin
  if (wstrb[0]) begin
    $write("%c", wdata[7:0]);
    $fflush();
  end
  response_valid <= 1'b1;
end

if (bvalid && bready) begin
  address_done <= 1'b0;
  response_valid <= 1'b0;
end
```

讲解：

- 字符输出后保持 `bvalid`。
- CPU 收到 B 响应后 store 才算提交。
- `$fflush()` 排除宿主 stdout 缓冲带来的假象。

**CLINT**

```verilog
mtime <= mtime + 64'd1;
if (arvalid && arready) begin
  response_valid <= 1'b1;
  response_data <= araddr[2] ? mtime[63:32] : mtime[31:0];
end
```

讲解：

- `mtime` 跟随 NPC 时钟增长。
- `0xa0000048` 返回低 32 位，`0xa000004c` 返回高 32 位。
- 设备行为从 C++ 地址特判迁移到 RTL。

**随机反压**

```cpp
static bool random_ready() {
  return axi_mode == AxiMode::Fixed || (next_random() & 3u) != 0;
}

static int response_delay() {
  return axi_mode == AxiMode::Fixed ? 0 : (int)(next_random() & 3u);
}
```

讲解：

- 固定模式用于快速回归。
- 随机模式制造握手延迟，暴露协议错误。

### 运行方式

B1 构建：

```sh
cd ~/ysyx-workbench/npc
make lint
make all
```

Access Fault：

```sh
make test-access-fault
```

本次复测：

```text
NPC performance: cycles=115 instructions=37 IPC=0.321739
HIT GOOD TRAP at pc = 0x80000034
```

随机 AXI DiffTest：

```sh
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
NPC_AXI_MODE=random NPC_AXI_SEED=20260621 \
  make ARCH=riscv32e-npc ALL="add load-store movsx" difftest
```

RT-Thread：

```sh
cd ~/Templates/rt-thread-am/bsp/abstract-machine
NPC_AXI_MODE=random NPC_AXI_SEED=23 make ARCH=riscv32e-npc run-batch
```

### Debug 过程和问题

1. IFU/LSU 共享仲裁器后 load 死锁：
   - 现象：SRAM `RVALID=1`，但 `RREADY=0`。
   - 原因：IFU R 响应组合直通后级，后级 load 又等待 LSU AR，形成循环等待。
   - 修复：IFU 增加响应缓冲，先接收 R，再输出给后级。

2. RT-Thread 最后 `msh />` 不输出：
   - 现象：命令都执行了，最后提示符缺失。
   - 原因：UART store 的 AXI 写事务没有完整等到 B 响应。
   - 修复：UART/LSU 均保持 BVALID 到 BREADY，store 以 B 握手为提交点。

3. DECERR 没触发异常：
   - 现象：Xbar 已返回错误，但 load 写回 0 或 store 看起来成功。
   - 原因：IFU/MEM 没消费 `rresp/bresp`。
   - 修复：响应完成点产生 Access Fault，并写 `mepc/mcause`。

4. 性能下降：
   - 单周期基线：IPC `1.000000`，Fmax `509.554 MHz`。
   - AXI-Lite：IPC `0.313294`，Fmax `376.892 MHz`。
   - 结论：总线化提供协议正确性，但当前没有提升性能；后续需要流水线、缓存和关键路径拆分。

## 附录 A：统一常用命令

NEMU 最小验证：

```sh
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
make ARCH=riscv32-nemu ALL=dummy run-batch
```

NEMU 全量 cpu-tests：

```sh
make ARCH=riscv32-nemu run-batch
```

NPC 基础验证：

```sh
make ARCH=riscv32e-npc ALL=dummy run-batch
```

NPC B1 Access Fault：

```sh
cd ~/ysyx-workbench/npc
make test-access-fault
```

RT-Thread：

```sh
cd ~/Templates/rt-thread-am/bsp/abstract-machine
NPC_AXI_MODE=random NPC_AXI_SEED=23 make ARCH=riscv32e-npc run-batch
```

## 附录 B：当前边界

- D6 的 ysyxSoC 接入没有在本报告中声明为已验证；当前主线已进入 B1 AXI-Lite 结构。
- B1 中 APB、PMA/PMP、完整 AXI4 burst/ID 属于讲义背景或后续 SoC 内容，不作为当前
  B1 必做实现。
- Chisel 目录是早期参考，当前权威实现是 `npc/vsrc` Verilog。

