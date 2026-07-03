# C1：工具和基础设施

## 学习记录

C1 的核心是建立调试基础设施。没有这些工具，后续 NPC 多周期、异常和总线问题会很难
定位。

## 实现记录

NEMU：

- sdb
- itrace/mtrace/ftrace/etrace
- DiffTest
- watchpoint

NPC：

- sdb
- itrace/mtrace/ftrace
- DiffTest
- 随机 AXI 反压检查

## 关键代码与讲解

NEMU 指令后统一处理 trace 和 DiffTest：

```c
static void trace_and_difftest(Decode *_this, vaddr_t dnpc) {
  if (ITRACE_COND) { log_write("%s\n", _this->logbuf); }
  iringbuf_record(_this->logbuf);
  IFDEF(CONFIG_DIFFTEST, difftest_step(_this->pc, dnpc));
  IFDEF(CONFIG_WATCHPOINT, check_watchpoints());
}
```

讲解：

- 一条指令执行完成后再推进 REF。
- watchpoint 也在指令后检查。
- 这样所有调试工具观察到的是同一个提交边界。

## 改动代码详解

### itrace ring buffer：保留最近上下文

```c
#define IRINGBUF_SIZE 32
static char iringbuf[IRINGBUF_SIZE][128];
static int iringbuf_head = 0;
static int iringbuf_cnt = 0;

static void iringbuf_record(const char *str) {
  strcpy(iringbuf[iringbuf_head], str);
  iringbuf_head = (iringbuf_head + 1) % IRINGBUF_SIZE;
  if (iringbuf_cnt < IRINGBUF_SIZE) {
    iringbuf_cnt++;
  }
}
```

这个缓冲区解决的是“程序已经跑飞了，但最后几条指令看不到”的问题。它只保存最近
32 条指令，空间小，出错时能快速回看上下文。`iringbuf_head` 指向下一次写入位置，
写满后覆盖最旧记录。

### ftrace：通过 ELF 符号把跳转变成函数调用关系

```c
if (s->logbuf[0]) {
  if (strstr(s->logbuf, "call") || strstr(s->logbuf, "jalr") ||
      strstr(s->logbuf, "jal")) {
    ftrace_record_call(s->pc, s->dnpc);
  } else if (strstr(s->logbuf, "ret")) {
    ftrace_record_ret(s->pc, s->dnpc);
  }
}
```

这里利用反汇编字符串判断 call/ret，再用 ELF 符号表把目标地址翻译成函数名。它不是
完整控制流分析，但对 AM 程序和 RT-Thread 的函数级调试已经足够。AM 脚本默认传入
`--ftrace $(IMAGE).elf`，所以只要打开 `CONFIG_FTRACE` 就能工作。

### mtrace：在物理内存访问边界记录

```c
IFDEF(CONFIG_MTRACE, if (MTRACE_COND) {
  log_write("mtrace W addr=" FMT_PADDR " len=%d data=" FMT_WORD
      " pc=" FMT_WORD "\n", addr, len, data, cpu.pc);
});
```

mtrace 放在 `paddr_read/write()` 内，而不是放在 load/store 指令内。这样所有物理
内存访问都会经过统一记录点，包括后续 MMU 翻译后的访问。记录 `pc` 很重要，因为
只知道地址不够，定位 bug 需要知道是哪条指令造成的访问。

### etrace：NEMU 侧非侵入观察异常

```c
IFDEF(CONFIG_ETRACE, {
  log_write("etrace cause=" FMT_WORD " (%s) epc=" FMT_WORD
      " target=" FMT_WORD "\n", NO, exception_name(NO), epc, cpu.mtvec);
});
```

把异常记录放在 `isa_raise_intr()` 中，而不是放在 AM CTE 中，原因是：

- 不改变客户程序行为。
- 即使程序还没进入 CTE 就崩了，也能看到异常入口信息。
- 能直接看到 `cause/epc/mtvec`，适合排查 `ecall/mret` 问题。

### DiffTest：提交后比较架构状态

```c
IFDEF(CONFIG_DIFFTEST, difftest_step(_this->pc, dnpc));
```

NEMU 作为 DUT 时，每条指令执行后让 REF 执行一条并比较寄存器和 PC。这个调用放在
trace 之后、watchpoint 之前，保证日志已经记录当前指令，同时又能尽早发现 REF/DUT
分歧。

## 运行方式

NPC trace：

```sh
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
NPC_TRACE=1 make ARCH=riscv32e-npc ALL=dummy run-batch
```

NEMU trace 通过 `make ISA=riscv32 menuconfig` 打开对应选项。

## Debug 心得

- 先看 itrace 判断控制流。
- 再看 mtrace 判断地址和数据。
- 函数调用错乱看 ftrace。
- 异常错乱看 etrace。
- NPC 多周期时只看 `commit_valid`，不要按任意时钟周期判断架构状态。
