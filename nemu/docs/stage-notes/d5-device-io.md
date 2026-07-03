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

- 设备测试失败先确认 `CONFIG_DEVICE=y`。
- 启动日志应出现 serial、rtc、keyboard 等 MMIO map。
- 串口不输出时，优先检查地址 `0xa00003f8`。
- timer 异常时，检查高低 32 位读取是否稳定。
