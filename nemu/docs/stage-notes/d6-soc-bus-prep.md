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

- 顶层端口是否匹配 SoC wrapper。
- PC 是否从 Flash 地址启动。
- `flash_read()` 字节序是否正确。
- hello/dummy 是否能在 SoC 环境运行。
