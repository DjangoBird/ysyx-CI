# D5：设备和输入输出

## 学习记录

D5 的重点是理解“设备也是地址空间的一部分”。CPU 访问普通内存走 pmem，访问设备地址
走 MMIO。

## 实现记录

NEMU：

```text
nemu/src/device/io/map.c
nemu/src/device/io/mmio.c
nemu/src/device/serial.c
nemu/src/device/timer.c
nemu/src/memory/paddr.c
```

NPC：

- 早期通过 C++ 特判串口/timer 地址。
- 当前 B1 版本 UART/CLINT 已迁移到 RTL AXI-Lite slave。

## 关键代码与讲解

NEMU 物理访存分发：

```c
word_t paddr_read(paddr_t addr, int len) {
  if (likely(in_pmem(addr))) {
    word_t ret = pmem_read(addr, len);
    return ret;
  }
  IFDEF(CONFIG_DEVICE, return mmio_read(addr, len));
  out_of_bound(addr);
  return 0;
}
```

讲解：

- `in_pmem(addr)` 判断是否访问物理内存。
- 非 pmem 地址交给 `mmio_read()`。
- 未开启设备且访问越界会直接 panic，便于定位错误地址。

## 改动代码详解

### MMIO map 的抽象

设备注册时会创建一个映射项：

```text
name
low/high
space
callback
```

内存系统只负责根据地址找到 map，再调用通用 `map_read/map_write`。设备行为放在
callback 中。这样 serial、rtc、keyboard 等设备可以复用同一套地址分发逻辑。

### 为什么 `map_write()` 先写后回调

写设备时，通用逻辑先把数据写到设备后端 `space`，再调用 callback。callback 可以
从 `space` 读取刚刚写入的值。例如串口 callback 读取写入的字符并输出。这个顺序
比“先 callback 后写 space”更符合寄存器写入语义。

### 设备访问和 DiffTest

MMIO 访问有副作用，REF 未必实现同样设备。对这种指令做 DiffTest 时，正确策略不是
让 REF 也访问设备，而是 DUT 执行后同步架构状态给 REF。NEMU/NPC 都遵循这个原则，
避免设备模型差异污染 ISA 正确性检查。

### 32 位机器读取 64 位 timer

timer/CLINT 一般提供 64 位计数器，但 RV32 一次只能读 32 位。稳妥读法是高-低-高：
如果两次高位不一致，说明低位读取期间发生了溢出，需要重读。简单测试不一定暴露这个
问题，但系统软件长期运行时会遇到。

## 运行方式

```sh
cd ~/ysyx-workbench/am-kernels/tests/am-tests
make ARCH=riscv32-nemu mainargs=t run-batch
make ARCH=riscv32-nemu mainargs=d run
make ARCH=riscv32-nemu mainargs=y run
```

## Debug 心得

### 场景 1：设备测试一开始就 panic 越界

现象：

```text
address out of bound
```

排查：

1. NEMU 是否打开 `CONFIG_DEVICE=y`。
2. 启动日志是否打印 MMIO map。
3. 访问地址是否落在设备范围。
4. `paddr_read/write()` 是否在非 pmem 地址调用 `mmio_read/write()`。

命令：

```bash
cd ~/ysyx-workbench/nemu
rg -n "CONFIG_DEVICE" .config include/generated/autoconf.h
```

如果 `CONFIG_DEVICE` 没开，非 pmem 地址会被当成越界，而不是设备访问。

### 场景 2：串口没有输出

串口输出链：

```text
putch()
  -> store to 0xa00003f8
  -> paddr_write()
  -> mmio_write()
  -> serial callback
  -> putc/flush
```

排查：

1. AM `putch()` 地址是否是 `0xa00003f8`。
2. NEMU 日志是否注册 serial map。
3. `paddr_write()` 是否走到 `mmio_write()`。
4. `map_write()` 是否先写 `space` 再调用 callback。
5. callback 是否输出低 8 位字符。

可以临时开 mtrace 或在 `paddr_write()` 下 gdb 断点，看地址是否正确。

### 场景 3：timer 测试不稳定

timer 读取链：

```text
io_read(AM_TIMER_UPTIME)
  -> __am_timer_uptime()
  -> 读 RTC/CLINT MMIO
  -> 转换成 us
```

排查：

1. 低 32 位和高 32 位地址是否正确。
2. RV32 是否使用 high-low-high 防撕裂读法。
3. 时间单位是否是微秒。
4. 是否把 host 时间、guest 周期和模拟频率混在一起。

如果时间偶尔倒退或跳变，优先怀疑 64 位计数读取过程中低位溢出。

### 场景 4：DiffTest 在设备访问处失败

MMIO 有副作用，REF 不一定有同样设备。判断：

- 如果失败 PC 是 load/store 到设备地址，不应让 REF 原样执行。
- DUT 执行 MMIO 后，应该同步 DUT 状态给 REF。
- 后续普通指令继续比较。

NPC 中这对应 `npc_difftest_skip_ref()`；NEMU 中也要避免把设备差异当 ISA 错误。

### 场景 5：键盘/设备测试没有输入

当前很多测试环境下键盘输入依赖 SDL/事件循环。排查：

1. 是否以交互模式运行，而不是完全无输入的 batch。
2. 设备初始化是否注册 keyboard map。
3. SDL/NVBoard 事件循环是否被调用。
4. AM `io_read(AM_INPUT_KEYBRD)` 是否被实际执行。

如果只是验证 timer/yield，不要把键盘无输入误判为 CPU 错误。
