# PA4 阶段 1：RV32 上下文切换与 RT-Thread 启动

本文记录 PA4.1 第一阶段在 RV32 NEMU 上的实现，包括异常机制、内核线程上下文、
`yield-os` 上下文切换和 RT-Thread 启动。Nanos-lite 及后续用户进程、虚拟内存内容
不在本文范围内。

相关提交：

```text
e76b36c PA4: implement RV32 context switching
777ed00 NEMU: add RISC-V exception trace
```

RT-Thread 位于独立仓库 `~/Templates/rt-thread-am`，相关提交：

```text
1f0b649 bsp: support RV32 AM context switching
```

## 1. 阶段目标

本阶段需要打通以下执行链：

```text
yield()
  -> ecall
  -> NEMU 保存 mepc/mcause 并跳转到 mtvec
  -> AM trap.S 保存 Context
  -> CTE 将异常转换为 EVENT_YIELD
  -> OS/RT-Thread 选择另一个 Context
  -> trap.S 切换 sp 并恢复新 Context
  -> mret
```

验证目标：

1. `am-kernels/kernels/yield-os` 持续输出 `ABAB...`。
2. RV32 RT-Thread 输出启动横幅和 `Hello RISC-V!`。

## 2. NEMU 异常支持

### 2.1 CSR 状态

在 `nemu/src/isa/riscv32/include/isa-def.h` 的 CPU 状态中增加：

```c
word_t mstatus, mtvec, mepc, mcause;
```

复位时初始化这些 CSR，其中 `mstatus` 使用 `0x1800`。这表示 MPP 为 M-mode，
也是讲义要求 RV32 配合 DiffTest 使用的初始值。

### 2.2 CSR 和系统指令

在 RV32 decode 中实现本阶段需要的指令：

- `ecall`
- `mret`
- `csrrw`
- `csrrs`

当前实现支持以下 CSR：

| CSR | 地址 |
| --- | --- |
| `mstatus` | `0x300` |
| `mtvec` | `0x305` |
| `mepc` | `0x341` |
| `mcause` | `0x342` |

`ecall` 执行：

```c
s->dnpc = isa_raise_intr(11, s->pc);
```

RV32 当前运行于 M-mode，因此环境调用的异常号为 11。`isa_raise_intr()` 保存
`mepc` 和 `mcause`，并返回 `mtvec` 作为下一条指令地址。

`mret` 将下一条 PC 设置为 `mepc`。在处理 `yield` 时，AM 会将保存的
`mepc` 增加 4，避免返回后再次执行同一条 `ecall`。

### 2.3 ETRACE

NEMU 增加了 `CONFIG_ETRACE`。异常进入 `isa_raise_intr()` 时记录：

```text
etrace cause=0x0000000b (environment call from M-mode)
       epc=0x800014a4 target=0x800014b0
```

ETRACE 使用原有 `log_write()`，因此：

- 输出写入 NEMU 日志文件，不直接写客户程序串口。
- 受 `CONFIG_TRACE_START` 和 `CONFIG_TRACE_END` 控制。
- 如果第一次 `yield()` 发生在跟踪区间之后，需要增大 `TRACE_END`。

## 3. AM Context 布局

`trap.S` 按以下顺序在栈上构造 Context 栈帧：

```text
gpr[0..31], mcause, mstatus, mepc, pdir
```

其中前四部分由 trap 入口保存，`pdir` 在本阶段只预留空间，不主动写入。

因此 `abstract-machine/am/include/arch/riscv.h` 必须使用完全一致的布局：

```c
struct Context {
  uintptr_t gpr[NR_REGS], mcause, mstatus, mepc;
  void *pdir;
};
```

结构体布局与汇编偏移不一致时，C 代码读取到的 `mcause`、`mepc` 和通用寄存器
都会错位。表现通常是无法识别 `EVENT_YIELD`、`mret` 跳转错误或恢复后直接跑飞。

`trap.S` 的 `CONTEXT_SIZE` 使用 `(NR_REGS + 4) * XLEN`，为 `pdir` 保留空间。
虽然当前阶段不使用虚拟地址空间，但 Context 的 C 定义和汇编栈帧仍应保持一致。

## 4. CTE 事件处理

RV32 `yield()` 将 `a7` 设置为 `-1` 后执行 `ecall`：

```c
asm volatile("li a7, -1; ecall");
```

CTE 根据 `mcause` 和保存的 `a7` 区分事件：

```c
case 11:
  if (c->GPR1 == (uintptr_t)-1) {
    ev.event = EVENT_YIELD;
    c->mepc += 4;
  } else {
    ev.event = EVENT_SYSCALL;
  }
  break;
```

RV32 ABI 中 `a7` 是 `x17`，所以 `GPR1` 定义为 `gpr[17]`。

## 5. 创建内核线程

`kcontext()` 在线程栈顶人工构造一个 Context：

```c
Context *kcontext(Area kstack, void (*entry)(void *), void *arg) {
  uintptr_t sp = (uintptr_t)kstack.end;
  sp = sp / sizeof(uintptr_t) * sizeof(uintptr_t);

  Context *c = (Context *)(sp - sizeof(Context));
  *c = (Context) { 0 };
  c->mepc = (uintptr_t)entry;
  c->mstatus = 0x1800;
  c->gpr[10] = (uintptr_t)arg;
  return c;
}
```

关键状态：

- `mepc = entry`：执行 `mret` 后从线程入口开始运行。
- `mstatus = 0x1800`：返回到 M-mode。
- `a0/x10 = arg`：按照 RV32 ABI 传递第一个函数参数。
- Context 放在对齐后的栈顶，只写入一个 Context。

## 6. 上下文切换

`__am_irq_handle()` 返回值是下一个要运行的 Context 指针。原有 `trap.S` 忽略了
该返回值，因此无论调度器返回什么，最终都会恢复原 Context。

修正方式是在恢复寄存器前切换栈指针：

```asm
mv a0, sp
call __am_irq_handle
mv sp, a0
```

之后所有 `LOAD` 和 `POP` 都从新 `sp` 指向的 Context 中读取。所以上下文切换的
本质不是复制所有寄存器，而是：

```text
保存 A 到 A 的栈 -> sp 指向 B 的 Context -> 从 B 的栈恢复
```

`yield-os` 的 PCB 保存 Context 指针：

```c
static Context *schedule(Event ev, Context *prev) {
  current->cp = prev;
  current = (current == &pcb[0] ? &pcb[1] : &pcb[0]);
  return current->cp;
}
```

验证：

```bash
cd $AM_KERNELS_HOME/kernels/yield-os
CCACHE_DISABLE=1 make ARCH=riscv32-nemu run-batch
```

预期输出：

```text
ABABABABABAB...
```

## 7. RT-Thread 适配

RT-Thread 使用独立仓库：

```text
~/Templates/rt-thread-am
```

### 7.1 `rt_hw_stack_init()`

RT-Thread 要求线程入口 `tentry(parameter)` 返回后执行 `texit()`，而 AM 的
`kcontext()` 要求入口函数不能返回。

实现中使用一个 RV32 汇编跳板：

```asm
__am_rt_thread_entry:
  jalr ra, s0, 0
  jalr zero, s1, 0
```

新 Context 中保存：

| 寄存器 | 内容 |
| --- | --- |
| `a0/x10` | `parameter` |
| `s0/x8` | `tentry` |
| `s1/x9` | `texit` |
| `mepc` | `__am_rt_thread_entry` |

跳板调用 `tentry(a0)`；`tentry` 返回后，无返回地跳转到 `texit`。

参数放在各线程自己的 Context 中，而不是共享全局变量，因此连续创建多个线程
不会互相覆盖启动参数。

### 7.2 RT-Thread 上下文切换

RT-Thread 传入的 `from` 和 `to` 都是 `Context **`：

```c
void rt_hw_context_switch(rt_ubase_t from, rt_ubase_t to);
```

当前单核、无抢占时钟中断模型使用全局变量将二级指针传递给 CTE 回调：

```c
switch_from = (Context **)from;
switch_to = (Context **)to;
yield();
```

`ev_handler()` 收到 `EVENT_YIELD` 后：

1. 将当前 Context 写入 `*switch_from`。
2. 返回 `*switch_to`。
3. 清空临时全局变量。

`rt_hw_context_switch_to()` 用于首次启动调度器，不需要保存 boot Context。

`rt_hw_context_switch_interrupt()` 尚未实现，因为本阶段没有时钟中断抢占，当前
RT-Thread 启动过程不会调用它。

### 7.3 RV32 freestanding 构建

本机使用 `riscv64-unknown-elf-gcc` 编译 RV32，但工具链没有完整 libc 头文件。
为了将范围限制在 RT-Thread 内核启动，本次配置关闭了依赖完整 POSIX/libc 的组件：

- DFS
- FinSH/MSH
- RTC 和 cputime
- `/dev/null`、`/dev/zero`、random
- UTest 和 ADT
- AM 应用集成

保留内核线程、调度器、堆、UART、设备框架和用户 main。BSP 添加了最小标准头兼容
层，并过滤 RT-Thread 的通用 libc 实现，实际字符串和格式化功能由 AM klib 提供。

这属于当前工具链和阶段范围下的裁剪，不代表这些 RT-Thread 组件本身不支持 RV32。

### 7.4 构建和运行

```bash
cd ~/Templates/rt-thread-am/bsp/abstract-machine
CCACHE_DISABLE=1 make ARCH=riscv32-nemu init
CCACHE_DISABLE=1 make ARCH=riscv32-nemu
CCACHE_DISABLE=1 make ARCH=riscv32-nemu run-batch
```

预期输出：

```text
am-apps.data.size = 0, am-apps.bss.size = 0
heap: [0x8001b000 - 0x88000000]

 \ | /
- RT -     Thread Operating System
 / | \     5.0.1
 2006 - 2022 Copyright by RT-Thread team
Hello RISC-V!
```

RT-Thread 进入 idle 后不会自行退出，`run-batch` 使用 `Ctrl-C` 或 `timeout` 结束是
正常行为。

## 8. 讲义问题回答

### 8.1 为什么把不同进程加载到不同位置很麻烦？

当前程序在链接时已经确定了代码和全局数据的地址。如果简单把同一个镜像复制到
另一个物理位置，指令中的绝对地址、全局变量地址和链接生成的符号仍可能指向原位置。
在没有位置无关代码、重定位处理或虚拟内存的情况下，不同程序还可能要求相同的链接
地址，因此无法直接同时放入一个物理地址空间。后续虚拟内存会让不同进程看到相同的
虚拟地址布局，并映射到不同物理页。

### 8.2 为什么不同执行流需要不同栈？

栈保存局部变量、返回地址、保存寄存器和 Context。共享同一个栈时，一个执行流的
函数调用和异常入栈会覆盖另一个执行流的数据；恢复时将取得错误的返回地址和寄存器，
最终导致数据破坏或控制流跑飞。独立栈也是通过切换 `sp` 恢复不同执行流的前提。

### 8.3 为什么叫内核线程而不是内核进程？

本阶段创建的执行流都运行在同一个内核地址空间，共享代码、全局数据和系统资源，
区别主要是各自拥有寄存器状态、Context 和栈。线程强调同一地址空间中的独立执行流；
进程通常还意味着独立的用户地址空间和资源管理，本阶段尚未实现这些机制。

### 8.4 机制和策略如何解耦？

AM CTE 提供“保存、选择并恢复 Context”的机制，它不决定运行哪个线程。`yield-os`
或 RT-Thread 调度器负责选择目标 Context，这是调度策略。更换轮转、优先级或其他
调度算法时，不需要修改底层 trap 保存和恢复机制。

### 8.5 为了从 trap 返回后执行线程入口，Context 需要满足什么条件？

至少需要：

1. `mepc` 指向入口或入口跳板。
2. `mstatus` 允许 `mret` 返回到预期特权级。
3. 参数寄存器符合 ABI，RV32 第一个参数放在 `a0/x10`。
4. Context 地址和恢复后的栈满足 ABI 对齐要求。
5. 所有未初始化寄存器具有可预测值，通常将 Context 清零。

### 8.6 如何在保持 `kcontext()` 只写一个 Context 的条件下传参？

RV32 参数通过寄存器传递，因此直接把 `arg` 写入 Context 中的 `a0` 即可，无需在
Context 外额外压栈。若参数数量更多，可以让 Context 从辅助跳板开始执行，并在其他
可用寄存器中保存入口和参数，跳板再按 ABI 调用真正入口。

### 8.7 多处理器同时使用上下文切换全局变量会发生什么？

两个处理器可能交叉覆盖 `switch_from/switch_to`。某个处理器进入异常回调时可能读取
到另一个处理器的目标，导致 Context 保存到错误 PCB，甚至恢复完全无关的线程。
解决方案包括每 CPU 数据、关闭本地中断并加锁，或将切换信息放入当前线程 PCB。

### 8.8 写入全局切换参数后发生时钟中断有什么问题？

时钟中断可能在 `yield()` 前抢占当前线程。另一个线程运行并修改同一组全局变量后，
原线程再进入 `ev_handler()` 时看到的已经不是自己的 `from/to`。当前实现仅在单核且
无时钟抢占的条件下成立；启用抢占后需要使用 PCB 私有数据或其他不可共享存储。

### 8.9 连续调用两次 `rt_hw_stack_init()` 时，为什么不能用全局变量保存启动参数？

第二次调用会覆盖第一次保存的 `tentry/parameter/texit`。第一个线程真正启动时将读取
第二个线程的参数。正确做法是把每个线程的启动信息放进自己的栈或 Context。本实现把
三项信息分别放入该线程 Context 的 `s0/a0/s1`，因此互不覆盖。

### 8.10 能否不使用全局变量完成 RT-Thread 上下文切换？

可以。讲义给出的方向是使用当前线程 PCB 的 `user_data` 保存切换信息：

1. 通过 `rt_thread_self()`获得当前 PCB。
2. 暂存原有 `user_data`。
3. 将 `from/to` 信息放入当前线程的私有字段。
4. `ev_handler()`从当前 PCB 读取。
5. 线程将来恢复并从切换函数返回前还原 `user_data`。

也可以扩展 AM 的事件或 Context 协议，但这会增加接口耦合。当前阶段硬件单核、无
抢占，因此使用全局变量是最小实现，但不是通用多核方案。

### 8.11 如果直接链接多个 AM 程序会有什么问题？

不同程序通常都定义 `_start`、`main`、AM/klib 符号和相同段名，直接链接会发生符号
冲突；各程序还假设拥有独立的数据区、BSS、堆和初始化过程。

`integrate-am-apps.py` 对每个程序分别构建，并通过 `objcopy`：

1. 为普通符号添加 `__am_<app>_` 前缀。
2. 将 AM/klib 公共符号重新映射到 RT-Thread 中共享的实现。
3. 将 `halt` 映射为退出当前 RT-Thread 线程。
4. 为应用段添加 `__am_apps` 前缀，集中管理 data/BSS。
5. 生成 Shell 包装入口和最终源码清单。

启动应用前，包装代码会恢复应用 data、清零 BSS 并设置独立 heap。当前阶段关闭了
AM 应用集成，因此只分析了脚本，未继续做运行验证。

## 9. 当前边界

已经完成：

- RV32 `ecall/mret` 和必要 CSR。
- `EVENT_YIELD`。
- `kcontext()` 和参数传递。
- CTE 根据返回 Context 切换 `sp`。
- `yield-os` 双线程切换。
- RT-Thread 线程创建、主动上下文切换和启动。
- ETRACE。

尚未完成：

- `rt_hw_context_switch_interrupt()`。
- 时钟中断和抢占调度。
- Nanos-lite 上下文切换。
- 用户进程、系统调用、VME 和分页。
- RT-Thread DFS、Shell、RTC 和 AM 应用集成。

## 10. 参考

- PA4.1 多道程序：<https://ysyx.oscc.cc/docs/ics-pa/4.1.html>
- RT-Thread AM：<https://github.com/NJU-ProjectN/rt-thread-am>
