# B1：总线、AXI-Lite、Access Fault 和性能

## 学习记录

B1 的核心是从“能运行”推进到“协议正确”。处理器不能再假设访存当周期完成，而是
必须通过 `valid/ready`、AXI-Lite 和响应错误处理来访问存储器和设备。

这一阶段最重要的心态变化是：访存不再是一个 C 函数调用。

早期仿真里我们可能写：

```text
data = pmem_read(addr)
pmem_write(addr, data, mask)
```

这会让人误以为“CPU 想读，数据马上回来；CPU 想写，写入马上完成”。真实硬件不是这样。
真实硬件里，CPU 和内存/外设之间隔着若干根线，双方只能在时钟边沿通过握手确认：

```text
我有请求 valid=1
我能接收 ready=1
valid && ready 同拍为 1，事务才真正发生
```

所以 B1 不是“把函数名换成 AXI 信号名”，而是把 CPU 的访存行为改成能承受等待、
反压、错误响应和多设备路由的状态机。

## 实现记录

当前权威实现是 Verilog `npc/vsrc`：

```text
npc/vsrc/minirv_core.v
npc/vsrc/npc_if_stage.v
npc/vsrc/npc_mem_stage.v
npc/vsrc/npc_axi_arbiter.v
npc/vsrc/npc_axi_xbar.v
npc/vsrc/npc_axi_uart.v
npc/vsrc/npc_axi_clint.v
npc/vsrc/npc_csr_file.v
npc/csrc/npc_step.cpp
```

结构：

```text
IFU --+
      +--> AXI4-Lite arbiter --> Xbar --> SRAM
LSU --+                              +-> UART
                                      +-> CLINT
                                      +-> DECERR
```

## 先理解总线

### `valid/ready` 到底表达什么

可以把每个通道看成一条单向传送带：

```text
发送方 ----payload----> 接收方
发送方 ----valid------> 接收方
发送方 <---ready------- 接收方
```

规则只有三条，但必须严格遵守：

1. 发送方有东西要传时拉高 `valid`。
2. 接收方能接收时拉高 `ready`。
3. 只有 `valid && ready` 的那个周期，payload 才被接收。

如果 `valid=1, ready=0`，说明发送方已经把货放到传送带上，但接收方还没收。此时
payload 必须保持不变。不能今天地址是 `0x80000000`，明天 ready 还没来就改成
`0x80000004`，否则接收方一旦拉高 ready，就不知道自己收到的是哪一个请求。

一个读地址通道的例子：

```text
cycle      1      2      3
arvalid   1      1      1
arready   0      0      1
araddr    A      A      A
fire      0      0      1
```

前两个周期请求没有被接收，所以 `araddr` 必须保持 A。第 3 周期 `fire=1`，地址 A
才真正交给 slave。

### SimpleBus 到 AXI-Lite 的关系

SimpleBus 可以先理解成两条通道：

```text
request:  req_valid, req_ready, addr, wdata, wmask, wen
response: resp_valid, resp_ready, rdata, error
```

AXI-Lite 把它拆得更细：

| AXI 通道 | 方向 | SimpleBus 中对应的概念 | 为什么拆出来 |
| --- | --- | --- | --- |
| AR | master -> slave | 读请求地址 | 读请求可以独立等待 |
| R | slave -> master | 读响应数据 | 数据回来时间不固定，还带错误响应 |
| AW | master -> slave | 写请求地址 | 写地址可先到 |
| W | master -> slave | 写请求数据/掩码 | 写数据可后到或先到 |
| B | slave -> master | 写完成响应 | store 必须知道写成功或失败 |

读事务：

```text
CPU:  ARVALID + ARADDR  ---->
MEM:  ARREADY           <----   地址被接收
MEM:  RVALID + RDATA    <----
CPU:  RREADY            ---->   数据被接收，load 才能提交
```

写事务：

```text
CPU:  AWVALID + AWADDR  ---->
MEM:  AWREADY           <----   写地址被接收

CPU:  WVALID + WDATA/WSTRB ---->
MEM:  WREADY               <---- 写数据被接收

MEM:  BVALID + BRESP    <----
CPU:  BREADY            ---->   写完成，store 才能提交
```

关键点：AW 和 W 没有规定必须同周期，也没有规定谁先谁后。我们的实现为了简化 Xbar，
让 Xbar 先接 AW 再接 W；但 MEMU 和外部 SRAM 模型都必须能处理反压，不能默认二者同拍。

### 为什么 store 不能在 W fire 后就提交

W fire 只说明写数据被 slave 收到了，不说明写操作已经成功完成。写可能失败，例如：

- 地址未映射。
- 设备只读。
- 下游返回 `DECERR`。

AXI-Lite 用 B 通道告诉 master 写事务最终结果。因此 store 的架构提交点必须是
`BVALID && BREADY`，不能是 AW fire，也不能是 W fire。

这点和 RT-Thread 最后提示符问题直接相关。UART 输出字符本质上是 MMIO store：

```text
store byte to 0xa00003f8
  -> AW/W 到 UART
  -> UART $write 字符
  -> UART 返回 B
  -> CPU 提交 store
```

如果 CPU 在 B 之前就继续跑，遇到反压时就可能丢字符或错乱提交。

### 当前 NPC 中一条指令如何走总线

普通 ALU 指令：

```text
IFU AR/R 取指
  -> IDU
  -> EXU 算结果
  -> MEMU 不访问总线，直接通过
  -> WBU 提交
```

load 指令：

```text
IFU AR/R 取指
  -> IDU/EXU 算 load 地址
  -> MEMU LSU AR 发读地址
  -> 等 R 返回
  -> MEMU 做 lb/lbu/lh/lhu/lw 扩展
  -> WBU 写回并提交
```

store 指令：

```text
IFU AR/R 取指
  -> IDU/EXU 算 store 地址、wdata、wmask
  -> MEMU LSU AW/W 发写地址和写数据
  -> 等 B 返回
  -> WBU 更新 PC，store 提交
```

取指和数据访存都要走总线，所以 IFU 和 LSU 会争同一个下游接口。这就是为什么需要
arbiter。

### Arbiter 和 Xbar 的区别

Arbiter 解决“多个上游抢一个下游”：

```text
IFU --+
      +--> Arbiter --> 一个下游接口
LSU --+
```

Xbar 解决“一个请求该去哪个设备”：

```text
一个上游请求 --> Xbar --> SRAM
                    +--> UART
                    +--> CLINT
                    +--> error
```

当前系统是先仲裁再译码：

```text
IFU/LSU -> Arbiter -> Xbar -> SRAM/UART/CLINT/error
```

这样外部 SRAM 只保留一套 AXI 接口，UART 和 CLINT 也像真实设备一样挂在地址空间里。

## 关键代码与讲解

### `top.v`：系统级连线

B1 之后，`top.v` 不再只是包一层 CPU，而是完整小系统的顶层：

```text
minirv_core
  -> npc_axi_arbiter
  -> npc_axi_xbar
       -> external SRAM port
       -> npc_axi_uart
       -> npc_axi_clint
       -> DECERR
```

顶层对 C++ 外壳暴露：

- 外部 SRAM AXI-Lite 端口：由 `npc_step.cpp` 模拟。
- `commit_*`：trace、DiffTest、性能统计。
- `dbg_x*`：sdb 和 DiffTest。
- `trap/trap_code`：程序结束。

这说明 B1 的“总线化”不是只改访存模块，而是把 CPU、内存、设备和仿真外壳之间的
边界整体改成总线协议。

### `minirv_core.v`：提交点和副作用门控

核心内部所有状态副作用都要被有效消息门控：

```text
commit_valid = mem_valid && mem_ready
```

含义：

- WBU 只在提交时更新 PC/GPR。
- CSR 只在对应异常或 CSR 指令真正通过时更新。
- trace/DiffTest 只在提交点观察。
- 下游反压时，上游不得重复 store 或重复写 CSR。

如果把副作用放在组合译码阶段，随机反压下会出现“同一条 store 写多次”或“CSR 被重复写”
这类固定延迟测不出的 bug。

IFU 响应缓冲：

```verilog
STATE_RESPONSE: begin
  if (axi_rvalid && axi_rready) begin
    instr_reg <= axi_rdata;
    access_fault_reg <= axi_rresp != 2'b00;
    state <= STATE_OUTPUT;
  end
end
```

讲解：先接收 R 响应并释放仲裁器，再把指令发给后级，避免 IFU/LSU 共享仲裁器后的
循环等待。

LSU 写通道：

```verilog
if (aw_fire) aw_done <= 1'b1;
if (w_fire) w_done <= 1'b1;
if ((aw_done || aw_fire) && (w_done || w_fire)) begin
  state <= STATE_WRITE_RESPONSE;
end
```

讲解：AW/W 独立握手，两个都完成后才等待 B 响应。store 以 B 握手为提交点。

Access Fault：

```verilog
assign access_fault = in_valid &&
                      (load_access_fault || store_access_fault);
assign access_fault_cause = load_access_fault ? 32'd5 : 32'd7;
assign wb_en_out = access_fault ? 1'b0 : wb_en_in;
assign pc_next_out = access_fault ? mtvec : pc_next_in;
```

讲解：错误在 R/B 响应完成点产生；load fault 禁止写回；下一 PC 跳到 `mtvec`。

Arbiter：

```verilog
assign out_arvalid = !read_busy && (lsu_arvalid || ifu_arvalid);
assign out_araddr = choose_lsu ? lsu_araddr : ifu_araddr;
assign lsu_arready = !read_busy && choose_lsu && out_arready;
assign ifu_arready = !read_busy && !choose_lsu && ifu_arvalid && out_arready;
```

讲解：读通道空闲时选择一个 master，并在 AR 握手后锁存 owner，直到 R 握手返回。
当前 `choose_lsu = lsu_arvalid`，即 LSU 优先；写通道只有 LSU，直接转发。

Xbar：

```verilog
function [1:0] decode_target;
  input [31:0] addr;
  begin
    if (addr >= 32'h8000_0000 && addr < 32'h8800_0000)
      decode_target = TARGET_MEM;
    else if (addr == 32'ha000_03f8)
      decode_target = TARGET_UART;
    else if (addr == 32'ha000_0048 || addr == 32'ha000_004c)
      decode_target = TARGET_CLINT;
    else
      decode_target = TARGET_ERROR;
  end
endfunction
```

讲解：Xbar 是硬件版 MMIO 地址译码。未映射或权限不匹配的访问走 error slave，并通过
`rresp/bresp` 变成 access fault。

UART：

```verilog
if (wvalid && wready) begin
  if (wstrb[0]) begin
    $write("%c", wdata[7:0]);
    $fflush();
  end
  response_valid <= 1'b1;
end
```

讲解：字符输出后保持 B 响应，CPU 收到 B 后 store 才算提交。

CLINT：

```verilog
mtime <= mtime + 64'd1;
if (arvalid && arready) begin
  response_valid <= 1'b1;
  response_data <= araddr[2] ? mtime[63:32] : mtime[31:0];
end
```

讲解：`mtime` 跟随 RTL 时钟增长，低/高 32 位通过地址 bit2 选择。

## 改动代码详解

### `npc_if_stage.v`：三态 FSM 让取指和流水后级解耦

```verilog
localparam [1:0] STATE_REQUEST  = 2'd0;
localparam [1:0] STATE_RESPONSE = 2'd1;
localparam [1:0] STATE_OUTPUT   = 2'd2;
```

三个状态分别代表：

- `STATE_REQUEST`：还没发出 AR 请求，驱动 `axi_arvalid=1`。
- `STATE_RESPONSE`：AR 已被接受，等待 R 响应。
- `STATE_OUTPUT`：R 已被锁存，等待后级 `out_ready`。

```verilog
assign axi_arvalid = (state == STATE_REQUEST);
assign axi_araddr = pc;
assign axi_rready = (state == STATE_RESPONSE);
assign out_valid = (state == STATE_OUTPUT);
```

这几条组合逻辑把状态机和 AXI/级间握手绑定起来。关键是 `axi_rready` 不依赖后级
`out_ready`，所以 IFU 可以先吃掉 R 响应，释放仲裁器。之前死锁就是因为 R 响应被
后级组合反压间接卡住。

### `npc_mem_stage.v`：load/store 分成不同等待路径

```verilog
wire memory_operation = in_valid && dmem_valid_in;
wire load_operation = memory_operation && !dmem_we_in;
wire store_operation = memory_operation && dmem_we_in;
```

这三条先把是否访存、读还是写拆开。后续状态机根据它们选择 AR/R 或 AW/W/B 路径。

```verilog
assign in_ready = (state == STATE_IDLE) ? (!dmem_valid_in && out_ready)
    : (state == STATE_READ_RESPONSE) ? (axi_rvalid && out_ready)
    : (state == STATE_WRITE_RESPONSE) ? (axi_bvalid && out_ready)
    : 1'b0;
```

`in_ready` 是 MEM 阶段能不能接收上一阶段消息的条件：

- 非访存指令在 IDLE 且下游 ready 时直接通过。
- load 必须等 R 响应来了，并且下游能接收。
- store 必须等 B 响应来了，并且下游能接收。
- 写请求阶段不 ready，防止上一条 store 尚未完成时新指令覆盖地址/数据。

### AW/W 捕获：解决独立通道乱序到达

```verilog
wire aw_fire = axi_awvalid && axi_awready;
wire w_fire = axi_wvalid && axi_wready;
```

`fire` 是握手完成的标准写法。只要 `valid && ready` 同拍为 1，该通道 payload 就被
对方接收。

```verilog
if (aw_fire) aw_done <= 1'b1;
if (w_fire) w_done <= 1'b1;
if ((aw_done || aw_fire) && (w_done || w_fire)) begin
  state <= STATE_WRITE_RESPONSE;
end
```

为什么条件里既有 `aw_done` 又有 `aw_fire`？因为非阻塞赋值在本拍末尾才更新。如果
本拍正好 AW 和 W 同时 fire，只看旧的 `aw_done/w_done` 会漏掉本拍事件。加入
`|| aw_fire` 和 `|| w_fire` 后，同拍完成也能立刻进入 B 响应等待。

### Access Fault：为什么禁止 load 写回

```verilog
assign wb_en_out = access_fault ? 1'b0 : wb_en_in;
assign pc_next_out = access_fault ? mtvec : pc_next_in;
```

load 访问错误时，`axi_rdata` 没有架构意义。如果仍然写回寄存器，会出现“异常发生了，
但寄存器已经被错误数据污染”的问题。因此 `access_fault` 时强制关写回，同时下一 PC
改为 `mtvec`。

### `npc_csr_file.v`：异常写 CSR 的优先级

```verilog
if (access_fault) begin
  mepc_reg <= access_fault_pc;
  mcause <= access_fault_cause;
end else if (ecall) begin
  mepc_reg <= ecall_pc;
  mcause <= 32'd11;
end
```

Access Fault 放在 `ecall` 前面，是为了给总线错误更高优先级。虽然正常情况下两者不
应同拍发生，但总线化之后异常来源变多，明确优先级可以避免 CSR 被后写覆盖。

### `npc_axi_arbiter.v`：读 owner 为什么要锁存

AXI 读地址和读数据不同周期发生。AR 发出去以后，R 返回时必须知道这个响应属于
IFU 还是 LSU。因此仲裁器用 `read_busy/read_owner` 记录当前未完成读事务：

```verilog
if (!read_busy && out_arvalid && out_arready) begin
  read_busy <= 1'b1;
  read_owner <= choose_lsu ? OWNER_LSU : OWNER_NONE;
end else if (read_busy && out_rvalid && out_rready) begin
  read_busy <= 1'b0;
end
```

如果不锁存 owner，R 返回时只能重新看当前 `ifu_arvalid/lsu_arvalid`，这会把响应送给
错误 master。

### `npc_axi_xbar.v`：为什么写路径要锁存 target

AXI 写地址 AW 和写数据 W 分离。W 通道本身没有地址，所以 Xbar 必须在 AW 阶段记录
写目标：

```verilog
if (in_awvalid && in_awready) begin
  write_target <= next_write_target;
  write_addr <= in_awaddr;
  write_state <= WRITE_DATA;
end
```

后续 W/B 都根据 `write_target` 路由。没有这个锁存，W 数据到达时 Xbar 不知道它应该
去 SRAM、UART 还是 error slave。

### `npc_axi_uart.v`：为什么 WREADY 要等 AW

```verilog
assign awready = !address_done && !response_valid;
assign wready = address_done && !response_valid;
assign bvalid = response_valid;
```

当前 UART 只实现一个写寄存器，因此设计成“先收地址，再收数据”。`wready` 依赖
`address_done`，保证 W 数据不会在没有地址上下文时被消费。输出字符后设置
`response_valid`，直到 CPU 拉高 `bready` 才清除。

### C++ AXI slave：随机延迟和响应保持

```cpp
prepare_read_response();
prepare_write_response();
arready = !mem_read.pending && !mem_read.rvalid && random_ready();
awready = !mem_write.aw_captured && !mem_write.pending &&
          !mem_write.bvalid && random_ready();
wready = !mem_write.w_captured && !mem_write.pending &&
         !mem_write.bvalid && random_ready();
```

C++ 侧模拟外部 SRAM。这里的 ready 不总是 1，而是可以随机拉低，用来模拟真实总线
反压。`pending/rvalid/bvalid` 防止在上一个响应还没被消费时接收新请求。

```cpp
if (mem_write.aw_captured && mem_write.w_captured &&
    !mem_write.pending && !mem_write.bvalid) {
  mem_write.pending = true;
  mem_write.delay = response_delay();
}
```

只有 AW 和 W 都捕获后，C++ SRAM 才真正安排一次写响应。这和 RTL LSU 的 AW/W 独立
握手互相验证：任意顺序到达都可以，但必须凑齐一对地址和数据才执行写入。

### `test-access-fault.S`：一个程序覆盖三类异常

这个测试依次触发 load fault、store fault、instruction fault，并在 trap handler 中
检查 `mcause`。它的价值是防止只实现了某一种错误路径：

- load fault 要验证禁止写回。
- store fault 要验证 B 响应错误。
- instruction fault 要验证 IFU 消费 `rresp`。

最终 good trap 说明三类异常都能进入 handler 并返回。

## 运行方式

构建：

```sh
cd ~/ysyx-workbench/npc
make lint
make all
```

Verilator 直接运行：

```sh
./build/top -b path/to/program.bin
./build/top path/to/program.bin     # 进入 sdb
```

随机反压：

```sh
NPC_AXI_MODE=random NPC_AXI_SEED=7 ./build/top -b path/to/program.bin
NPC_AXI_DEBUG=1 NPC_AXI_MODE=random NPC_AXI_SEED=7 ./build/top -b path/to/program.bin
```

NVBoard：

```sh
export NVBOARD_HOME=/path/to/NVBoard
make nvboard
make NPC_GUI=1 run IMG=path/to/program.bin
```

说明：

- Verilator 用于把 RTL 编译成可执行仿真模型。
- C++ SRAM slave 在 `npc_step.cpp` 中响应顶层 AXI。
- NVBoard 只观察绑定到 `top.nxdc` 的顶层端口；当前主要是 LED。

Access Fault：

```sh
make test-access-fault
```

本次复测：

```text
NPC performance: cycles=115 instructions=37 IPC=0.321739
HIT GOOD TRAP at pc = 0x80000034
```

随机 AXI DiffTest：

```sh
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
NPC_AXI_MODE=random NPC_AXI_SEED=20260621 \
  make ARCH=riscv32e-npc ALL="add load-store movsx" difftest
```

## Debug 心得

总线问题不要一开始就看全部波形。先按现象分组，再看对应通道。
通用调试流程、NEMU/NPC gdb、trace 和波形使用见 [C3 调试技巧](c3-debug.md)；本节只
展开 B1 总线相关问题。

### 场景 1：程序卡死，没有新提交

现象：

```text
commit_valid 长时间为 0
NPC_AXI_DEBUG 周期性打印某些通道 valid/ready
```

第一步打开调试输出：

```sh
cd ~/ysyx-workbench/npc
NPC_AXI_DEBUG=1 NPC_AXI_MODE=random NPC_AXI_SEED=7 \
  ./build/top -b path/to/program.bin
```

判断：

| 现象 | 优先怀疑 |
| --- | --- |
| `ARVALID=1, ARREADY=0` | 下游不接收读地址，检查 C++ SRAM/Xbar/arbiter |
| `RVALID=1, RREADY=0` | CPU 不接收读响应，检查 IFU/MEMU `rready` |
| `AWVALID=1, AWREADY=0` | 写地址没被接收，检查 Xbar 写状态 |
| `WVALID=1, WREADY=0` | 写数据没被接收，检查 W 是否等 AW 或 slave 忙 |
| `BVALID=1, BREADY=0` | CPU 不接收写响应，检查 MEMU store 提交条件 |

典型 load 死锁：

```text
RVALID=1, RREADY=0
LSU 又在等待 ARREADY
```

这通常说明 IFU 的 R 响应被后级组合反压卡住。修复方式就是当前代码中的 `OUTPUT`
状态：先锁存 R，再把指令交给后级。

### 场景 2：load-store 测试错

先用 fixed 模式定位语义：

```sh
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
NPC_AXI_MODE=fixed make ARCH=riscv32e-npc ALL=load-store run-batch
```

再用 random 模式验证协议：

```sh
NPC_AXI_MODE=random NPC_AXI_SEED=20260621 \
  make ARCH=riscv32e-npc ALL=load-store difftest
```

语义错误看这条链：

```text
EXU: rs1 + imm 是否正确
EXU: 对齐地址 {addr[31:2], 2'b00} 是否正确
EXU: store wmask/wdata 是否按地址低位移位
MEMU: load_byte_off 是否来自原始地址低位
MEMU: lb/lbu/lh/lhu 符号扩展或零扩展是否正确
WBU: wb_en/wb_idx/wb_data 是否在提交时写回
```

协议错误看这条链：

```text
AR fire 后是否只等一个 R
R fire 前 load 是否没有提交
AW/W 是否都 fire 后才生成 B
B fire 前 store 是否没有提交
valid && !ready 时 payload 是否稳定
```

### 场景 3：UART 少字符或 RT-Thread 少 `msh />`

UART 输出不是普通 printf，而是 MMIO store 到 `0xa00003f8`。

排查顺序：

1. EXU 是否生成 `dmem_we=1`，地址是否是 `0xa00003f8`。
2. Xbar 是否把 AW 地址译码到 `TARGET_UART`。
3. UART 是否先收到 AW，再收到 W。
4. `wstrb[0]` 是否为 1。
5. UART 是否执行 `$write("%c", wdata[7:0])` 和 `$fflush()`。
6. UART 是否保持 `bvalid` 到 CPU 拉高 `bready`。
7. MEMU 是否在 B fire 后才让 store 提交。

如果第 5 步已经输出字符，但最终 shell 行缺失，重点看第 6/7 步。字符输出和架构提交
是两个概念，B 响应没有完成时 CPU 不应该认为 store 完成。

### 场景 4：DECERR 出现但没有异常

错误响应路径：

```text
Xbar TARGET_ERROR
  -> read:  RRESP != OKAY
  -> write: BRESP != OKAY
  -> IFU/MEMU 识别响应错误
  -> CSR 写 mepc/mcause
  -> pc_next = mtvec
```

如果没有异常：

- 取指错误：看 IFU 是否锁存 `axi_rresp`，是否把 `instruction_access_fault` 送到 EXU。
- load 错误：看 MEMU 的 `load_access_fault` 是否成立。
- store 错误：看 MEMU 的 `store_access_fault` 是否成立。
- CSR 错误：看 `npc_csr_file.v` 是否在提交点写 `mepc/mcause`。

验证命令：

```sh
cd ~/ysyx-workbench/npc
make test-access-fault
```

这个测试不是只看能不能 bad trap，而是 handler 会检查三类 `mcause`，最后 good trap。

### 场景 5：DiffTest 报 PC 或寄存器不一致

先读报错中的 `after pc=...`，它表示刚提交的 DUT 指令。

```text
DiffTest: register mismatch after pc=0x8000....
```

处理步骤：

1. 打开对应 `.txt` 反汇编，找到这个 PC。
2. 判断指令类型：ALU、branch、load、store、CSR、MMIO。
3. ALU 错看 EXU。
4. load 错看 MEMU 扩展和 RDATA。
5. branch/jump 错看 `pc_next`。
6. MMIO 附近错看是否应该 `skip_ref`。

注意：trap 指令本身通常不做普通 DiffTest，因为 DUT 和 REF 的仿真退出协议可能不同。

### 场景 6：性能下降

B1 之后 IPC 下降是正常结果，因为一条指令至少经历取指握手，load/store 还要等数据
响应。当前实现的目标是协议正确和可接外设，不是性能最优。

性能分析看三组数：

- `cycles`：总周期。
- `instructions`：提交指令数。
- `IPC = instructions / cycles`。

如果要优化，先看 stall 计数：

```text
AXI-Lite stalls: AR=... R=... AW=... W=... B=...
```

哪个通道 stall 高，就说明对应方向等待多。后续优化方向不是“删掉总线”，而是流水线、
缓存、缩短关键路径和减少等待周期。
