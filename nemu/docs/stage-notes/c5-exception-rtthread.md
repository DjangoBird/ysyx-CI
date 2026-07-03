# C5：异常处理和 RT-Thread

## 学习记录

C5 把异常、上下文和系统软件连起来。重点是理解 `ecall -> trap.S -> CTE -> schedule
-> mret` 这条链路。

## 实现记录

NEMU：

- `mstatus/mtvec/mepc/mcause`
- `ecall/mret/csrrw/csrrs`
- etrace

AM：

- Context 布局
- `kcontext()`
- `trap.S` 保存和恢复上下文

NPC：

- CSR 文件
- `mcycle/mcycleh/mvendorid/marchid`
- RT-Thread 启动

## 关键代码与讲解

CTE 识别 yield：

```c
if (c->GPR1 == (uintptr_t)-1) {
  ev.event = EVENT_YIELD;
  c->mepc += 4;
} else {
  ev.event = EVENT_SYSCALL;
}
```

`trap.S` 切换 Context：

```asm
mv a0, sp
call __am_irq_handle
mv sp, a0

LOAD t1, OFFSET_STATUS(sp)
LOAD t2, OFFSET_EPC(sp)
csrw mstatus, t1
csrw mepc, t2
```

讲解：

- `__am_irq_handle()` 返回下一个 Context 指针。
- `mv sp, a0` 才是真正切换上下文的位置。
- `mepc += 4` 避免 `mret` 后再次执行同一条 `ecall`。

## 改动代码详解

### Context 布局必须和 `trap.S` 完全一致

AM 的 C 结构体：

```c
struct Context {
  uintptr_t gpr[NR_REGS], mcause, mstatus, mepc;
  void *pdir;
};
```

汇编保存布局：

```asm
#define CONTEXT_SIZE  ((NR_REGS + 4) * XLEN)
#define OFFSET_CAUSE  ((NR_REGS + 0) * XLEN)
#define OFFSET_STATUS ((NR_REGS + 1) * XLEN)
#define OFFSET_EPC    ((NR_REGS + 2) * XLEN)
```

这里最容易出错。C 代码通过 `c->mcause/c->mepc` 读字段，汇编通过固定偏移保存字段。
只要两边顺序不一致，CTE 就会读到错位数据，表现为：

- `mcause` 错，无法识别 `EVENT_YIELD`。
- `mepc` 错，`mret` 后跳飞。
- 通用寄存器错，线程参数或返回地址异常。

### `yield()` 在 RV32E 和 RV32I 中使用不同寄存器

```c
void yield() {
#ifdef __riscv_e
  asm volatile("li a5, -1; ecall");
#else
  asm volatile("li a7, -1; ecall");
#endif
}
```

RV32I ABI 中 syscall/yield 标记通常放 `a7/x17`；RV32E 没有 `x17`，所以改用
`a5/x15`。对应 `GPR1` 宏也必须按架构区分，否则 CTE 会把 yield 当成普通 syscall。

### `kcontext()` 人工构造第一次被恢复的现场

```c
Context *c = (Context *)(sp - sizeof(Context));
*c = (Context) { 0 };
c->mepc = (uintptr_t)entry;
c->mstatus = 0x1800;
c->gpr[10] = (uintptr_t)arg;
return c;
```

新线程不是从 trap 进入的，但调度器会把它当成一个即将 `mret` 恢复的 Context。
因此要手工构造：

- `mepc = entry`：`mret` 后进入线程入口。
- `mstatus = 0x1800`：返回 M-mode。
- `a0/x10 = arg`：按 ABI 传第一个参数。
- 其他寄存器清零，避免随机栈内容影响线程。

### `trap.S` 为什么要先保存旧 Context 再调用 C

```asm
addi sp, sp, -CONTEXT_SIZE
MAP(REGS, PUSH)
csrr t0, mcause
csrr t1, mstatus
csrr t2, mepc
STORE t0, OFFSET_CAUSE(sp)
STORE t1, OFFSET_STATUS(sp)
STORE t2, OFFSET_EPC(sp)
```

进入异常后，当前线程的寄存器还在硬件寄存器里。如果直接调用 C 函数，C 编译器会
覆盖 caller-saved 寄存器，旧线程现场就丢了。所以必须先完整压栈，再把 `sp` 作为
`Context *` 传给 `__am_irq_handle()`。

### `mepc += 4` 是 yield 能继续运行的关键

```c
if (c->GPR1 == (uintptr_t)-1) {
  ev.event = EVENT_YIELD;
  c->mepc += 4;
}
```

`ecall` 本身是一条 4 字节指令。异常进入时 NEMU/NPC 保存的 `mepc` 是 `ecall` 的
地址。如果不加 4，`mret` 后会再次执行同一条 `ecall`，程序表现为一直 trap，线程
无法向后推进。

## 运行方式

NEMU yield-os：

```sh
cd ~/ysyx-workbench/am-kernels/kernels/yield-os
make ARCH=riscv32-nemu run-batch
```

NPC RT-Thread：

```sh
cd ~/Templates/rt-thread-am/bsp/abstract-machine
NPC_AXI_MODE=random NPC_AXI_SEED=23 make ARCH=riscv32e-npc run-batch
```

## Debug 心得

### 场景 1：执行 `yield()` 后没有进入异常

排查：

1. `yield()` 是否真的执行 `ecall`。
2. NEMU/NPC 是否实现 `ecall` 译码。
3. `mtvec` 是否已经由 `cte_init()` 写成 `__am_asm_trap`。
4. `isa_raise_intr()` 或 CSR 文件是否写入 `mepc/mcause`。
5. `dnpc/pc_next` 是否跳到 `mtvec`。

工具：

```bash
make ARCH=riscv32-nemu run-batch
less build/nemu-log.txt   # 看 etrace
```

如果 etrace 没有记录，问题在执行到 CTE 之前；如果 etrace 有但 handler 没执行，查
`mtvec` 或 trap 入口地址。

### 场景 2：yield 死循环

现象：不断进入同一个 `ecall`，线程没有向后执行。

根因通常是没有：

```c
c->mepc += 4;
```

排查：

1. etrace 中 `epc` 是否总是同一个地址。
2. `__am_irq_handle()` 是否识别为 `EVENT_YIELD`。
3. RV32E 下 `GPR1` 是否对应 `a5`，RV32I 下是否对应 `a7`。
4. handler 返回前 `mepc` 是否已经加 4。

### 场景 3：上下文没有切换，仍回到原线程

关键代码：

```asm
mv a0, sp
call __am_irq_handle
mv sp, a0
```

如果缺少 `mv sp, a0`，即使调度器返回了另一个 Context，trap.S 仍会从旧栈恢复旧线程。

排查：

1. 在 `schedule()` 中打印或用 gdb 查看返回的 Context 指针是否变化。
2. 看 `trap.S` 是否把 `a0` 移到 `sp`。
3. 看恢复后的 `mepc` 是否来自新 Context。
4. 如果 `sp` 切了但寄存器错，检查 Context 布局和 `CONTEXT_SIZE`。

### 场景 4：第一次切到新线程就跑飞

优先查 `kcontext()` 构造的初始现场：

```text
mepc = entry
mstatus = 0x1800
a0/x10 = arg
sp 对齐
Context 只放在栈顶
```

如果入口地址错，`mret` 后直接跳飞；如果 `mstatus` 错，DiffTest 或特权返回可能异常；
如果 `a0` 错，线程输出参数会错。

### 场景 5：RV32E yield 被识别成 syscall

RV32E 没有 `a7/x17`，所以：

```c
#ifdef __riscv_e
asm volatile("li a5, -1; ecall");
#else
asm volatile("li a7, -1; ecall");
#endif
```

同时 `riscv.h` 中 `GPR1` 要匹配：

```c
#ifdef __riscv_e
#define GPR1 gpr[15] // a5
#else
#define GPR1 gpr[17] // a7
#endif
```

如果只改 `yield()` 或只改 `GPR1`，CTE 都会读错寄存器。

### 场景 6：RT-Thread 最后提示符缺失

先区分上下文问题和 UART 问题：

上下文路径：

```text
ecall -> mtvec -> trap.S -> __am_irq_handle -> mret
```

UART 路径：

```text
rt_kprintf -> putch -> store 0xa00003f8 -> UART -> B response -> store commit
```

如果 RT-Thread 已经能执行命令，只是最后少 `msh />`，更可能是 UART/store 提交问题。
检查：

1. UART AW/W 是否都收到。
2. `$write` 后是否 `$fflush()`。
3. BVALID 是否保持到 BREADY。
4. MEMU 是否等 B fire 后提交 store。

### 场景 7：RT-Thread native 能编译，riscv32e-npc 不行

不要混用 Context 结构。native 的 `Context` 和 RISC-V 的 `Context` 不同；RV32E 还只有
16 个 GPR。排查：

1. `#ifdef __riscv_e` 是否覆盖 RV32E 特例。
2. `Context` 是否有 `gpr[NR_REGS]`。
3. RT-Thread BSP 是否访问了不存在的 `gpr[17]`。
4. 编译参数是否是 `-march=rv32e_zicsr -mabi=ilp32e`。
