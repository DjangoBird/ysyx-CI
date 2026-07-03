# PA4 阶段 1 / B1 调试历史

本文记录这次 PA4 阶段 1 到启动 RT-Thread 之间的主要排障过程。重点不是复述最终
实现，而是把“遇到什么问题、为什么会这样、最后怎么修”的过程固定下来，方便之后
回看。

## 1. 先把异常链路做成可观测

最先补的是异常可观测性。

在 NEMU 侧加入 `etrace` 后，可以直接看到异常从哪里发生、`epc` 是什么、异常原因
是什么，而不需要在 CTE 或 RT-Thread 里额外插 `printf()`。这样做的价值有两个：

1. 不改变程序行为。
2. 程序即使在异常入口前就崩掉，仍然能看到异常踪迹。

这一步的作用主要是“定位入口”。后面无论是 `ecall`、`ebreak`、`mret`，还是上下文
切换相关问题，都先用异常链路判断处理器有没有走到预期的 trap 路径。

## 2. 把提交点和 trace 对齐

第二个问题是 trace 不能按“每个时钟周期”理解，而必须按“指令提交”理解。

当 NPC 从简单单周期模型改成带握手和外部访存等待的模型后，取指请求、访存请求和
最终提交不再是同一个周期。之前如果按请求拍推进 REF，DiffTest 会提前执行，结果
就是：

- 该提交的指令没对上。
- `itrace` 和 `mtrace` 的顺序会乱。
- 异常指令附近更难定位。

最后的处理是把 `commit_valid` 作为唯一的提交边界，只在 WBU 真正提交时：

- 记录 `itrace`。
- 记录 `mtrace`。
- 推进 DiffTest REF 一条指令。

这样请求阶段的等待不会污染 trace 结果。

## 3. AXI4-Lite 写通道不要默认 AW/W 同拍

这一条是最容易写错的地方。

AXI4-Lite 的 AW 和 W 是独立通道，不能假设地址和数据一定同周期到达，也不能假设
先后顺序固定。最初如果把它当成“两个端口同时来”的简化接口，短延迟或反压一出现，
就会把写事务写丢，或者写成半包。

最终的修法是：

1. AW 和 W 分开锁存。
2. 只要某一条通道握手成功，就撤销那条通道的 `valid`。
3. 等两条都到齐后，只执行一次写入。
4. 最后等 B 响应，再把 store 当成真正提交。

这一步修好后，串口输出这类 MMIO store 才稳定。

## 4. `msh />` 最后一个提示符的问题

RT-Thread 跑起来以后，前面的命令都能执行，但最后一行 `msh />` 有时不出现。

这个现象本质上不是 shell 本身坏了，而是 UART 输出链路里有写事务没有被完整保住。
在 AXI 写通道还不正确的时候，终端字符依赖的 MMIO store 会被反压打断，或者在
AW/W 到达顺序变化时丢失。

早期 SimpleBus 版本曾在 `pmem_write32()` 中对 UART 地址逐字节输出并
`fflush(stdout)`，这能排除主机 stdout 缓冲造成的假象，但它不是当前最终架构。

B1 加入 Xbar 后，UART 已迁移为 [npc_axi_uart.v](../vsrc/npc_axi_uart.v) 中的 RTL
AXI4-Lite slave，C++ 内存模型不再识别 UART 地址。当前修复点是：

1. UART slave 分别接收 AW 和 W，不能要求二者同拍。
2. `wstrb[0]` 有效时只输出 `wdata[7:0]`。
3. BVALID 一直保持到 BREADY，CPU 收到 B 响应后 store 才提交。

修完后，RT-Thread 在随机反压下也能稳定走完预置命令序列，并输出最后的
`msh />`。由于 shell 随后等待输入，测试进程不会自动退出。

## 5. RT-Thread 和上下文切换

为了验证不只是“能启动”，还检查了异常、`mret` 和上下文切换是否正常。

这里用的是 `yield-os` 和 RT-Thread 两条线：

- `yield-os` 用来验证异常入口和返回链路。
- RT-Thread 用来验证 `mret`、定时器/串口相关路径以及 shell 交互。

如果这两条线有问题，通常优先看：

1. trap 入口是否真的进去了。
2. `mepc/mstatus` 是否按预期保存和恢复。
3. 提交点是否只在 `commit_valid` 发生。
4. MMIO store 是否被错误地提前提交或重复执行。

## 6. 这次验证的结论

最后得到的结论是：

- `etrace` 负责把异常入口可观测化。
- `commit_valid` 负责把 trace 和 DiffTest 对齐到提交点。
- AXI4-Lite 的 AW/W 独立握手是串口和普通 store 都能正确工作的前提。
- RT-Thread 的最后提示符问题，最终归结为 MMIO 写事务建模不完整。

如果后续再碰到类似的“程序看起来少输出一行”问题，优先怀疑的是外设写事务、
提交边界和 stdout 刷新，而不是先怀疑应用层逻辑。

## 7. 接入共享仲裁器后的 load 死锁

把 IFU/LSU 两套独立 slave 合并为共享 AXI 总线后，最初普通指令能运行，但程序会
稳定停在第一批 load 指令。外部 SRAM 表现为 `RVALID=1, RREADY=0`。

原因不是仲裁器选错 master，而是原 IFU 把取指响应组合直通后级：

1. IFU 持有读仲裁权，等待后级拉高 `RREADY`。
2. 返回的指令组合译码成 load，LSU 立即发出新的 AR 请求。
3. LSU 必须等 IFU 释放仲裁器。
4. IFU 的 `RREADY` 又被阻塞在该 load 的 LSU 请求之后。

这形成了 `IFU R -> pipeline -> LSU AR -> arbiter -> IFU R` 的循环等待。修复是在 IFU
中增加响应缓冲：R 到达时无条件完成握手并锁存指令，释放仲裁器；下一状态再通过
级间 `valid/ready` 发送指令。修复后固定和随机反压测试均恢复通过。

## 8. STA 工具兼容问题

本机 `yosys-sta` 的脚本面向较新的 Yosys，而系统可直接取得的是 Yosys 0.33，
缺少脚本使用的 `clockgate` 命令。为避免把工具版本问题混入 RTL，NPC 增加了精简的
`scripts/synth.tcl`，保留读取 RTL、综合、Nangate45 映射、常量单元映射和网表输出。

iEDA 对总线端口和常量 assign 的网表解析也更严格，因此网表输出前还需要：

- `hilomap` 将常量映射为 `LOGIC0_X1/LOGIC1_X1`。
- `splitnets -ports` 将总线端口拆成标量。

完成这两步后可以稳定生成 STA 报告。加入 Access Fault 后重新评估，375 MHz
通过，380 MHz 出现 setup 违例。

## 9. `DECERR` 已产生但处理器没有异常

第一次加入 Xbar 时，未映射地址已经返回 `rresp/bresp=DECERR`，但 IFU 和 MEMU
只是声明了响应端口，没有消费它们。这会造成两个问题：

1. load 可能把无效的 `rdata=0` 写入寄存器并继续执行。
2. store 看起来正常提交，软件不知道写入实际失败。

修复时把错误处理放在“响应完成点”，而不是请求点：

- IFU 锁存 RRESP，错误时产生 instruction access fault，`mcause=1`。
- MEMU 在 load 的 R 握手时产生 `mcause=5`，并禁止寄存器写回。
- MEMU 在 store 的 B 握手时产生 `mcause=7`。
- CSR 只在对应消息真正 `fire` 时更新 `mepc/mcause`。
- 下一 PC 改为 `mtvec`，由软件处理后执行 `mret`。

为避免只验证某一类错误，新增 `tests/access-fault.S`，一个程序依次触发三种异常，
异常处理函数检查 `mcause` 后返回，最后以 good trap 结束。

## 10. 总线化前后性能不能凭感觉判断

B1 要求同时评估单周期基线和总线化结果。最初文档只记录了当前 NPC，没有基线，
因此无法回答总线重构实际带来了什么变化。

通过临时 worktree 复测总线改造前提交 `66de2c2`，结果为：

```text
single-cycle:  IPC=1.000000, Fmax=509.554 MHz
AXI-Lite NPC:  IPC=0.313294, Fmax=376.892 MHz
```

当前实现不但 IPC 降低，Fmax 也下降，因此 MicroBench 估算时间从 `0.392 s` 增加到
`1.669 s`。这不是测量错误，而是说明当前设计仍把较长组合逻辑留在同一周期，增加
总线并不会自动缩短关键路径。后续优化必须用流水寄存器、缓存或关键路径拆分来量化。

## 11. 被 `.gitignore` 隐藏的头文件

`minirv_defs.vh` 在本机一直存在，所以 Verilator 能编译；但 `npc/.gitignore` 的
宽泛规则会忽略 `.vh`，新克隆无法得到 opcode/funct3 定义。审计 B1 可复现性时才
暴露这个问题。

修复是在白名单中加入 `!*.vh`。这类问题说明“当前目录能编译”不等于“仓库可复现”，
验收时必须同时检查 `git status` 和 `git ls-files`。

## 12. 后续调试的固定切入点

这次之后，NPC 问题基本可以先按提交边界拆分：

1. 没有 `commit_valid`：优先看 IFU/MEMU 状态机、AXI ready/valid、仲裁器 owner、
   Xbar 目标锁存。
2. 有提交但寄存器错：看 EXU 指令语义、MEMU load 扩展、WBU 写回门控。
3. 有提交但输出错：看 MMIO store 是否等到 B 响应，UART 是否保持 BVALID，
   C++ 是否把对应提交错误地当成普通 DiffTest。
4. DiffTest 报错：先找 `after pc` 对应的反汇编，再看这条指令提交后的 DUT/REF
   差异，不要从 bad trap 末尾倒猜。

具体逐文件代码路径整理在 [npc-code-walkthrough.md](npc-code-walkthrough.md)。那份文档
用于读代码，这份文档用于回看排障过程。
