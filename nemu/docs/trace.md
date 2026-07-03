# NEMU Trace 机制说明

这份文档说明 NEMU 里的 trace 相关能力是怎么工作的，以及各段代码分别负责什么。

NEMU 这里的 trace 主要包括四类：

1. `itrace`，指令跟踪。
2. `ftrace`，函数调用跟踪。
3. `mtrace`，内存访问跟踪。
4. `etrace`，异常处理跟踪。

它们都属于调试基础设施，本质上是在执行过程中把“发生了什么”记录出来，方便定位 bug。

## 总体配置

这些功能都由 `Kconfig` 控制，开关位于 `Testing and Debugging` 菜单下。

关键配置项在 [Kconfig](Kconfig) 中：

```text
config TRACE
  bool "Enable tracer"
  default y

config ITRACE
  depends on TRACE && TARGET_NATIVE_ELF && ENGINE_INTERPRETER
  bool "Enable instruction tracer"
  default y

config MTRACE
  depends on TRACE && TARGET_NATIVE_ELF
  bool "Enable memory tracer"
  default n

config ETRACE
  depends on TRACE && TARGET_NATIVE_ELF
  bool "Enable exception tracer"
  default n

config FTRACE
  depends on TARGET_NATIVE_ELF && ENGINE_INTERPRETER
  bool "Enable function tracer"
  default n
```

这些配置有几个重要含义：

1. `TRACE` 是总开关。
2. `ITRACE` 默认开启，主要用于指令级调试。
3. `MTRACE` 默认关闭，因为输出量很大。
4. `ETRACE` 默认关闭，用于观察异常进入路径。
5. `FTRACE` 主要用于 native 目标下的函数调用/返回分析。

## 执行链路

trace 的核心入口在 [src/cpu/cpu-exec.c](src/cpu/cpu-exec.c)。NEMU 每执行一条指令，都会经过 `exec_once()` 和 `trace_and_difftest()`。

### 1. 执行单条指令

`exec_once()` 负责：

1. 设置当前 PC。
2. 调用 ISA 层执行一条指令。
3. 更新 `cpu.pc`。
4. 如果开启了 `itrace`，生成反汇编字符串。
5. 如果开启了 `ftrace`，根据反汇编结果识别 call / ret。

代码位置在 [src/cpu/cpu-exec.c](src/cpu/cpu-exec.c)：

```c
static void exec_once(Decode *s, vaddr_t pc) {
  s->pc = pc;
  s->snpc = pc;
  isa_exec_once(s);
  cpu.pc = s->dnpc;
#ifdef CONFIG_ITRACE
  char *p = s->logbuf;
  p += snprintf(p, sizeof(s->logbuf), FMT_WORD ":", s->pc);
  int ilen = s->snpc - s->pc;
  int i;
  uint8_t *inst = (uint8_t *)&s->isa.inst;
#ifdef CONFIG_ISA_x86
  for (i = 0; i < ilen; i ++) {
#else
  for (i = ilen - 1; i >= 0; i --) {
#endif
    p += snprintf(p, 4, " %02x", inst[i]);
  }
  ...
  disassemble(p, s->logbuf + sizeof(s->logbuf) - p,
      MUXDEF(CONFIG_ISA_x86, s->snpc, s->pc), (uint8_t *)&s->isa.inst, ilen);
#ifdef CONFIG_FTRACE
  if (s->logbuf[0]) {
    if (strstr(s->logbuf, "call") || strstr(s->logbuf, "jalr") || strstr(s->logbuf, "jal")) {
      ftrace_record_call(s->pc, s->dnpc);
    } else if (strstr(s->logbuf, "ret")) {
      ftrace_record_ret(s->pc, s->dnpc);
    }
  }
#endif
#endif
}
```

### 2. trace 与调试功能的统一入口

`trace_and_difftest()` 会在每条指令执行后做后续处理：

1. 写入 itrace 环形缓冲区。
2. 在需要时把单步 trace 打到屏幕上。
3. 触发 DiffTest。
4. 检查 watchpoint。

代码位置同样在 [src/cpu/cpu-exec.c](src/cpu/cpu-exec.c)：

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

## ITRACE：指令跟踪

`itrace` 的目标是记录“每条指令执行了什么”。它主要依赖两部分：

1. `disassemble()` 生成反汇编文本。
2. 环形缓冲区保存最近若干条 trace。

### 1. 反汇编

反汇编实现位于 [src/utils/disasm.c](src/utils/disasm.c)：

```c
void disassemble(char *str, int size, uint64_t pc, uint8_t *code, int nbyte) {
  cs_insn *insn;
  size_t count = cs_disasm_dl(handle, code, nbyte, pc, 0, &insn);
  assert(count == 1);
  int ret = snprintf(str, size, "%s", insn->mnemonic);
  if (insn->op_str[0] != '\0') {
    snprintf(str + ret, size - ret, "\t%s", insn->op_str);
  }
  cs_free_dl(insn, count);
}
```

`init_disasm()` 会在启动时动态加载 Capstone 库，并按当前 ISA 选择正确的反汇编模式。

### 2. 指令环形缓冲区

`cpu-exec.c` 里维护了一个 ring buffer，用来保存最近的指令文本：

```c
#ifdef CONFIG_ITRACE
#define IRINGBUF_SIZE 32
static char iringbuf[IRINGBUF_SIZE][128];
#endif
```

记录和打印逻辑如下：

```c
static void iringbuf_record(const char *str) {
  strcpy(iringbuf[iringbuf_head], str);
  iringbuf_head = (iringbuf_head + 1) % IRINGBUF_SIZE;
  if (iringbuf_cnt < IRINGBUF_SIZE) {
    iringbuf_cnt++;
  }
}

static void iringbuf_display() {
  if (iringbuf_cnt == 0) {
    return;
  }

  puts("Instruction ring buffer:");
  int start = (iringbuf_head - iringbuf_cnt + IRINGBUF_SIZE) % IRINGBUF_SIZE;
  for (int i = 0; i < iringbuf_cnt; i++) {
    puts(iringbuf[(start + i) % IRINGBUF_SIZE]);
  }
}
```

这部分的用途有两个：

1. 在单步运行时，可以直接看到最近执行的指令。
2. 在异常、watchpoint、死循环检测触发时，可以打印上下文。

## FTRACE：函数跟踪

`ftrace` 的目标是把函数调用和返回关系打印出来，适合看程序控制流。

### 1. 符号解析

`ftrace` 的实现位于 [src/utils/ftrace.c](src/utils/ftrace.c)。它会读取 ELF 的符号表，筛出函数符号并排序：

```c
static FuncSym *funcs = NULL;
static size_t func_cnt = 0;
```

初始化时会解析 ELF：

```c
void init_ftrace(const char *elf_file) {
  if (!elf_file) return;
  FILE *f = fopen(elf_file, "rb");
  ...
  if (func_cnt > 1) qsort(funcs, func_cnt, sizeof(FuncSym), cmp_func);
  Log("ftrace: loaded %zu function symbols from %s", func_cnt, elf_file);
}
```

### 2. call / ret 记录

当 `exec_once()` 中识别到 `call`、`jalr`、`jal` 或 `ret` 时，会调用：

```c
void ftrace_record_call(vaddr_t pc, vaddr_t target) {
  vaddr_t symaddr = 0;
  const char *name = ftrace_find_symbol(target, &symaddr);
  ...
  log_write("%s" FMT_WORD ": call [%s@" FMT_WORD "]\n", ind, pc, name, symaddr);
}

void ftrace_record_ret(vaddr_t pc, vaddr_t target) {
  if (ftrace_depth > 0) ftrace_depth--;
  vaddr_t symaddr = 0;
  const char *name = ftrace_find_symbol(target, &symaddr);
  ...
  log_write("%s" FMT_WORD ":   ret  [%s]\n", ind, pc, name);
}
```

输出会写入日志文件，和 itrace 一样，是给调试用的，不是给最终程序逻辑用的。

## MTRACE：内存跟踪

`mtrace` 在 [src/memory/paddr.c](src/memory/paddr.c) 里，监控物理内存读写。

### 1. 读内存时记录

```c
word_t paddr_read(paddr_t addr, int len) {
  if (likely(in_pmem(addr))) {
    word_t ret = pmem_read(addr, len);
    IFDEF(CONFIG_MTRACE, if (MTRACE_COND) {
      log_write("mtrace R addr=" FMT_PADDR " len=%d data=" FMT_WORD " pc=" FMT_WORD "\n",
          addr, len, ret, cpu.pc);
    });
    return ret;
  }
  ...
}
```

### 2. 写内存时记录

```c
void paddr_write(paddr_t addr, int len, word_t data) {
  if (likely(in_pmem(addr))) {
    IFDEF(CONFIG_MTRACE, if (MTRACE_COND) {
      log_write("mtrace W addr=" FMT_PADDR " len=%d data=" FMT_WORD " pc=" FMT_WORD "\n",
          addr, len, data, cpu.pc);
    });
    pmem_write(addr, len, data);
    return;
  }
  ...
}
```

这类 trace 对定位数组越界、栈破坏、错误访存很有帮助，但输出量通常很大，所以默认关闭。

## ETRACE：异常跟踪

`etrace` 在 [src/isa/riscv32/system/intr.c](src/isa/riscv32/system/intr.c) 里，
记录处理器进入异常处理时的关键信息。

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

输出内容：

```text
etrace cause=... (...) epc=... target=...
```

含义：

1. `cause` 是异常号，例如 RV32 当前 `ecall` from M-mode 为 11。
2. `epc` 是异常发生时的 PC。
3. `target` 是异常处理入口 `mtvec`。

`etrace` 的价值是非侵入式：它在 NEMU 侧输出，不需要在客户程序或 AM 的 CTE 中
插入 `printf()`。

## 启动阶段的关联

trace 相关能力是在启动阶段初始化的。`monitor.c` 里会根据配置决定是否初始化反汇编器、是否装载调试模块。

代码在 [src/monitor/monitor.c](src/monitor/monitor.c)：

```c
void init_monitor(int argc, char *argv[]) {
  ...
  init_isa();
  long img_size = load_img();
  init_difftest(diff_so_file, img_size, difftest_port);
  init_sdb();

  IFDEF(CONFIG_ITRACE, init_disasm());
  ...
}
```

也就是说：

1. `itrace` 依赖 `init_disasm()`，所以它只在支持的构建目标上启用。
2. `ftrace` 依赖运行时传入的 ELF 符号信息。
3. `mtrace` 依赖内存访问路径本身。
4. `etrace` 依赖异常进入 `isa_raise_intr()`。

## 如何使用

### 开启/关闭

这些功能主要通过 `menuconfig` 配置：

```text
Testing and Debugging
  [*] Enable tracer
  [*] Enable instruction tracer
  [ ] Enable memory tracer
  [ ] Enable exception tracer
  [ ] Enable function tracer
```

### 观察输出

1. `itrace` 会在执行时打印或缓存最近指令。
2. `ftrace` 会在日志里输出 call / ret 事件。
3. `mtrace` 会在日志里输出读写内存事件。
4. `etrace` 会在日志里输出异常事件。

如果你想看单步下的指令轨迹，可以在 monitor 里用：

```text
(nemu) si 1
```

然后配合 `itrace` 或日志文件观察。

## 与调试的关系

trace 不是单独的功能，而是和调试能力耦合的基础设施：

1. watchpoint 负责表达式级检查。
2. itrace 负责指令级上下文。
3. ftrace 负责函数级调用链。
4. mtrace 负责访存级行为。
5. etrace 负责异常级行为。

把这几层拼起来，就能更快地定位“程序为什么跑飞了”。

## 代码索引

- [src/cpu/cpu-exec.c](src/cpu/cpu-exec.c): trace 主链路、指令环形缓冲区、call/ret 识别。
- [src/utils/disasm.c](src/utils/disasm.c): 反汇编支持。
- [src/utils/ftrace.c](src/utils/ftrace.c): 函数符号解析与 call/ret 输出。
- [src/memory/paddr.c](src/memory/paddr.c): 访存 trace。
- [src/isa/riscv32/system/intr.c](src/isa/riscv32/system/intr.c): 异常 trace。
- [src/monitor/monitor.c](src/monitor/monitor.c): 启动初始化。
- [Kconfig](Kconfig): trace 相关配置项。
