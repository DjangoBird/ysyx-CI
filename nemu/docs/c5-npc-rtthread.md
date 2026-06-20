# C5：在 RV32E NPC 上启动 RT-Thread

本文记录 C5 阶段在 RV32E NPC 上补充 CSR、异常处理和 AM 支持，并最终启动
RT-Thread 的实现。NEMU 上的 PA4 上下文切换见
[`pa4-stage1-rv32.md`](pa4-stage1-rv32.md)。

## 1. 完成状态

当前 NPC 已支持：

- 64 位 `mcycle`，通过 `mcycle`/`mcycleh` 分别读取低、高 32 位。
- `mvendorid = 0x79737978`，按字节解释为 `ysyx`。
- `marchid = 22040000`。
- `mstatus`、`mtvec`、`mepc` 和 `mcause`。
- `csrrw`、`csrrs`、`ecall` 和 `mret`。
- RV32E AM 的 CTE、`kcontext()` 和上下文切换。
- 串口 MMIO 输出。
- AM timer 通过 `mcycle/mcycleh` 提供单调计数。

完整 RT-Thread 已在 `riscv32e-npc` 上启动，能够执行预置 MSH 命令，并在命令
执行完后输出最终的 `msh />` 提示符。

## 2. NPC CSR

NPC 增加独立 CSR 模块，包含以下寄存器：

| CSR | 地址 | 行为 |
| --- | --- | --- |
| `mstatus` | `0x300` | 复位值为 `0x1800`，可读写 |
| `mtvec` | `0x305` | 异常入口，可读写 |
| `mepc` | `0x341` | 异常 PC，可读写 |
| `mcause` | `0x342` | 异常原因，可读写 |
| `mcycle` | `0xb00` | 64 位周期计数器低 32 位 |
| `mcycleh` | `0xb80` | 64 位周期计数器高 32 位 |
| `mvendorid` | `0xf11` | 固定为 `0x79737978` |
| `marchid` | `0xf12` | 固定为 `22040000` |

`mcycle` 每个 NPC 时钟周期加一。AM timer 将当前 NPC 的一个周期按一微秒处理，
因此可使用该计数器完成本阶段的软件计时测试；这不是实际流片频率的换算模型。

TRM 在调用 `main()` 前读取并打印两个 ID：

```text
mvendorid = 0x79737978, marchid = 22040000
```

## 3. CSR 和系统指令

SYSTEM 指令增加：

- `csrrw rd, csr, rs1`：返回 CSR 原值，并写入 `rs1`。
- `csrrs rd, csr, rs1`：返回 CSR 原值；`rs1 != x0` 时写入按位或结果。
- `ecall`：保存 `mepc = pc`、`mcause = 11`，下一条 PC 跳到 `mtvec`。
- `mret`：下一条 PC 跳到 `mepc`。

本阶段只需要处理来自 M-mode 的环境调用，因此 `mcause` 固定为 11。`mret` 暂未
实现完整特权级和中断使能位切换，只提供当前 RT-Thread 主动上下文切换所需语义。

## 4. RV32E AM 异常处理

RV32E 只有 `x0` 到 `x15`。AM 使用 `a5/x15` 传递 yield 标记：

```c
asm volatile("li a5, -1; ecall");
```

CTE 收到 `mcause == 11` 后，根据保存的 `a5` 区分事件：

```c
if (c->GPR1 == (uintptr_t)-1) {
  ev.event = EVENT_YIELD;
  c->mepc += 4;
} else {
  ev.event = EVENT_SYSCALL;
}
```

`mepc` 增加 4 后，`mret` 不会再次执行同一条 `ecall`。

`kcontext()` 在栈顶构造清零的 Context，并初始化：

- `mepc = entry`
- `mstatus = 0x1800`
- `a0/x10 = arg`

异常处理函数返回的是下一个 Context。`trap.S` 必须执行 `mv sp, a0`，再从新的
栈帧恢复 CSR 和通用寄存器，否则调度器虽然选择了新线程，处理器仍会恢复旧线程。

## 5. NPC 串口

NPC AM 的 `putch()` 向 `0xa00003f8` 写入字符。NPC 运行时拦截该 MMIO 地址，
按照写掩码输出有效字节并刷新标准输出。这样 RT-Thread 启动横幅、日志和 MSH 提示符
都能立即显示，命令执行结束后也不会因宿主缓冲区而丢失最后一个提示符。

## 6. 验证

### 6.1 CSR ID

```bash
cd ~/ysyx-workbench/am-kernels/kernels/hello
CCACHE_DISABLE=1 make ARCH=riscv32e-npc run-batch
```

关键输出：

```text
mvendorid = 0x79737978, marchid = 22040000
Hello, AbstractMachine!
HIT GOOD TRAP
```

### 6.2 `mcycle/mcycleh`

```bash
cd ~/ysyx-workbench/am-kernels/tests/am-tests
CCACHE_DISABLE=1 make ARCH=riscv32e-npc mainargs=t run-batch
```

RTC 日期仍是未实现的默认值，但秒数连续增长，说明 64 位周期计数读取正常：

```text
1900-0-0 00:00:00 GMT (1 second).
1900-0-0 00:00:00 GMT (2 seconds).
...
```

### 6.3 异常和上下文切换

```bash
cd ~/ysyx-workbench/am-kernels/kernels/yield-os
CCACHE_DISABLE=1 make ARCH=riscv32e-npc run-batch
```

预期持续输出：

```text
ABABABABABAB...
```

这条测试覆盖 `csrrw/csrrs`、`mtvec`、`ecall`、`mcause`、`mepc`、`mret` 和
Context 切换。

### 6.4 RT-Thread

```bash
cd ~/Templates/rt-thread-am/bsp/abstract-machine
CCACHE_DISABLE=1 make ARCH=riscv32e-npc
CCACHE_DISABLE=1 make ARCH=riscv32e-npc run-batch
```

实际验证输出包括：

```text
mvendorid = 0x79737978, marchid = 22040000
am-apps.data.size = 13036, am-apps.bss.size = 155048
heap: [0x80297000 - 0x88000000]

 \ | /
- RT -     Thread Operating System
 / | \     5.0.1
 2006 - 2022 Copyright by RT-Thread team
[I/utest] utest is initialize success.
[I/utest] total utest testcase num: (0)
Hello RISC-V!
msh />help
RT-Thread shell commands:
...
msh />utest_list
[I/utest] Commands list :
msh />
```

UART 驱动会自动注入测试命令。全部命令执行后 RT-Thread 仍在 shell 中等待输入，
因此使用 `Ctrl-C` 或 `timeout` 结束 `run-batch` 是正常行为。

## 7. 当前边界

本阶段实现的是启动 RT-Thread 所需的最小机器态异常机制。尚未实现：

- 外部中断和时钟中断。
- `rt_hw_context_switch_interrupt()` 和抢占调度。
- 完整的 RISC-V 特权级状态转换。
- NPC RTC 设备。

## 8. 参考

- C5 讲义：<https://ysyx.oscc.cc/docs/2407/c/5.html>
- PA4.1：<https://ysyx.oscc.cc/docs/ics-pa/4.1.html>
