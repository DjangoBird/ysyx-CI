# D4：RTL minirv NPC

## 学习记录

D4 从软件模拟器进入 RTL 处理器。核心不是一次实现完整 CPU，而是先把 IFU、IDU、EXU、
LSU、WBU 的基本边界划清楚，并通过 `ebreak` 让程序控制仿真结束。

## 实现记录

早期 minirv 已完成，当前已演进到 RV32E NPC。关键模块：

```text
npc/vsrc/npc_if_stage.v
npc/vsrc/npc_id_stage.v
npc/vsrc/npc_ex_stage.v
npc/vsrc/npc_mem_stage.v
npc/vsrc/npc_wb_stage.v
npc/csrc/npc_step.cpp
```

## 关键代码与讲解

C++ 运行时根据 trap 结束仿真：

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

- `ebreak` 不只是调试指令，在 AM 中也是程序退出协议。
- `a0 == 0` 表示 good trap。
- 这种方式比固定运行 N 个周期更稳定。

## 改动代码详解

### reset 不是只拉高信号，而是跑若干周期

```cpp
void reset(int n) {
  dut.rst = 1;
  while (n-- > 0) {
    (void)single_cycle();
  }
  dut.rst = 0;
}
```

RTL 中 PC、寄存器堆、总线状态机都是时序逻辑。reset 需要保持若干周期，让所有
寄存器进入确定状态。如果只在组合上拉一下 `rst`，有些状态可能没有在时钟边沿被
真正复位，后续表现为 PC 随机、trap 随机或访存状态异常。

### 运行循环只关心 trap

```cpp
int run_simulation() {
  reset(10);
  while (!Verilated::gotFinish()) {
    if (single_cycle()) break;
  }
  ...
}
```

模拟器不应该猜程序要跑多少周期。AM 程序结束时会执行 `ebreak`，硬件把它转换成
`trap`，C++ runtime 收到后停止。这种协议比“运行固定 N 周期”更可靠，也支持不同
程序长度。

### trap code 与 AM `halt(code)` 对齐

AM 的 `halt(code)` 最终把 `code` 放到 `a0`，然后执行 `ebreak`。NPC 在 `ebreak`
时把 `a0` 输出为 `trap_code`。因此：

- `trap_code == 0` 是 good trap。
- 非 0 是 bad trap。
- CPU 测试可以通过返回值表达成功或失败。

## 运行方式

```sh
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
make ARCH=riscv32e-npc ALL=dummy run-batch
```

## Debug 心得

- PC 能动但不结束时，先查 `ebreak` 是否译码。
- trap code 错时，查 `a0` 是否正确连接到顶层。
- AM/NPC 的 PC 复位地址必须和镜像加载地址一致。
