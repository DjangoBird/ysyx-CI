# D6：D 阶段流片准备

## 学习记录

D6 引入总线和 SoC 接入。它的重点是理解处理器不能永远通过 DPI-C 直接访问内存，
最终需要通过稳定的总线协议访问 Flash、SRAM 和外设。

## 实现记录

当前仓库历史中已完成 SimpleBus，并继续演进到 B1 AXI4-Lite。当前主线 NPC 已不是
D6 讲义中的 ysyxSoC SimpleBus 接口。

## 关键代码与讲解

D6 的 SimpleBus 思路在 B1 中被 AXI-Lite 覆盖。当前报告把具体总线代码集中放在
[B1 笔记](b1-bus-axi.md)。

## 改动代码详解

### 从 DPI-C 访存到总线访存

早期 NPC 访存可以抽象成：

```text
addr -> pmem_read32(addr) -> rdata
```

这种模型默认当周期完成，没有反压、没有错误响应。D6/B1 后变成：

```text
request valid + addr
  -> 等待 request ready
  -> 等待 response valid
  -> 读取 rdata/rresp
```

这要求 RTL 能停住流水级，并在响应回来后再提交指令。也正是这个变化引出了
`valid/ready`、SimpleBus、AXI-Lite 和 Access Fault。

### 为什么当前不把 ysyxSoC 写成已完成

当前主线 NPC 已进入 B1 AXI-Lite 结构，顶层接口和 D6 讲义中的 ysyxSoC
SimpleBus/Flash 接口不同。报告中只说明 SimpleBus 历史完成和当前边界，避免把
AXI-Lite 工作误标成 D6 SoC 验收。

## 运行方式

SimpleBus 和 AXI-Lite 演进记录：

```text
npc/docs/b1-stage-bus-refactor.md
```

## Debug 心得

当前没有把 ysyxSoC 接入声明为已验证。若要按 D6 做流片检查，需要单独确认：

### 场景 1：SoC wrapper 接不上 NPC 顶层

先确认接口类型。当前主线是 B1 AXI-Lite 顶层，不是 D6 SimpleBus/Flash wrapper。
如果按 D6 接 ysyxSoC，需要单独检查：

```text
时钟/复位名
取指地址接口
数据访存接口
Flash 读接口
SRAM/外设接口
trap/debug 输出
```

端口不匹配时不要硬连。先画出 wrapper 需要什么，再决定是写桥接模块还是切回对应历史
提交验证 D6。

### 场景 2：PC 没从 Flash 启动

D6 SoC 通常要求从 Flash 地址启动，例如 `0x30000000`。当前 AM/NPC 主线通常从
`0x80000000` PMEM 启动。排查：

1. reset PC 是多少。
2. linker script 的入口地址是多少。
3. 镜像被放到了 Flash 还是 SRAM。
4. 第一条取指是否访问 Flash。

如果 PC 是 `0x80000000`，但 SoC 只在 `0x30000000` 放程序，取到的指令必然错误。

### 场景 3：Flash 读出来机器码字节序错

RISC-V 指令是小端存储。排查：

```bash
xxd -g 4 -l 32 program.bin
objdump -d program.elf | head
```

如果 `objdump` 第一条机器码是 `0x00000297`，bin 中字节应类似：

```text
97 02 00 00
```

Flash 接口如果按错误字节序拼 32 位，CPU 会取到完全不同指令。

### 场景 4：SimpleBus/AXI 思路混用

D6 SimpleBus 和 B1 AXI-Lite 都用握手，但通道语义不同：

- SimpleBus 常见是 request/response 两通道。
- AXI-Lite 是 AR/R/AW/W/B 五通道。

如果把 AXI 的 AW/W/B 简化成 SimpleBus 同拍写，random 反压和真实 SoC 下会失败。
文档中把 D6 写成历史阶段，就是为了避免把两套接口混为一谈。

### 场景 5：hello/dummy 在 SoC 环境不结束

排查链：

```text
reset PC
Flash 第一条指令
串口地址
ebreak/trap 输出
仿真结束条件
```

很多 SoC 环境没有 NEMU/NPC 的 `trap` 端口，程序结束需要用仿真器约定或串口输出判断。
因此不能直接把 NPC 的 `HIT GOOD TRAP` 经验套到 SoC wrapper 上。
