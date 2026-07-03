# B1：总线、AXI-Lite、Access Fault 和性能

## 学习记录

B1 的核心是从“能运行”推进到“协议正确”。处理器不能再假设访存当周期完成，而是
必须通过 `valid/ready`、AXI-Lite 和响应错误处理来访问存储器和设备。

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

## 关键代码与讲解

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

- load 死锁：IFU R 响应不能被后级组合反压阻塞，必须先锁存。
- 串口少字符：检查 AW/W/B，store 必须等 B。
- DECERR 没异常：检查 `rresp/bresp` 是否被 IFU/MEM 消费。
- 性能下降是当前实现的真实结果，后续优化要靠流水线、缓存和关键路径拆分。
