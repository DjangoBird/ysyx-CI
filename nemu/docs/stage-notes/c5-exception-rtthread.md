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

- 没进入异常：看 etrace 或 CSR 写入。
- yield 死循环：看 `mepc += 4`。
- 上下文不切换：看 `mv sp, a0`。
- RT-Thread 最后提示符缺失：检查 UART store 是否等到 B 响应提交。
