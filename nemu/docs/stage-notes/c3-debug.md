# C3：调试技巧

## 学习记录

C3 是调试方法学习。关键认识是：问题要按层次定位，不要只靠 printf。

## 实现记录

当前调试层次：

- 应用输出：AM/RT-Thread 行为。
- 函数层：ftrace。
- 指令层：itrace。
- 访存层：mtrace。
- 异常层：etrace。
- 硬件层：Verilator 波形和 NPC trace。
- 协议层：随机 AXI 反压。

## 关键代码与讲解

AXI payload 稳定性检查：

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

- `valid && !ready` 期间 payload 必须稳定。
- 随机反压下这个检查能抓到固定延迟测不出的协议 bug。

## 改动代码详解

### 随机 ready 用来破坏错误假设

```cpp
static bool random_ready() {
  return axi_mode == AxiMode::Fixed || (next_random() & 3u) != 0;
}
```

固定 ready 下，很多错误设计也能跑：例如默认 AW/W 同拍、默认 R 响应立刻被接收。
随机 ready 会让这些隐含假设失效，从而暴露协议 bug。

### stall 计数定位卡住的通道

```cpp
if (dut.mem_axi_arvalid && !dut.mem_axi_arready) ++ar_stalls;
if (dut.mem_axi_rvalid && !dut.mem_axi_rready) ++r_stalls;
if (dut.mem_axi_awvalid && !dut.mem_axi_awready) ++aw_stalls;
if (dut.mem_axi_wvalid && !dut.mem_axi_wready) ++w_stalls;
if (dut.mem_axi_bvalid && !dut.mem_axi_bready) ++b_stalls;
```

如果程序长时间无提交，stall 计数能告诉你问题在哪个通道。比如 `R` stall 很高，
通常优先看 IFU/MEM 是否正确拉高 `rready`；`B` stall 很高则看 store 提交路径。

### 长时间无提交时的 AXI debug

```cpp
if (!dut.rst && std::getenv("NPC_AXI_DEBUG") != nullptr &&
    cycle_count > last_commit_cycle + 32 &&
    ((cycle_count - last_commit_cycle) % 32 == 0)) {
  std::fprintf(stderr, "AXI debug cycle=... AR=%u/%u R=%u/%u ...\n", ...);
}
```

这个输出只在设置 `NPC_AXI_DEBUG` 且长时间没有提交时触发，避免正常运行刷屏。它适合
定位死锁时哪个通道 valid/ready 长期不握手。

## 运行方式

```sh
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
NPC_AXI_MODE=random NPC_AXI_SEED=7 \
  make ARCH=riscv32e-npc ALL="dummy add bit shift if-else load-store movsx" run-batch
```

## Debug 心得

- 先看提交边界，再看时钟周期。
- 少输出字符先怀疑 MMIO store 或 B 响应。
- load 死锁先查 IFU 是否及时释放 R 通道和仲裁器。
