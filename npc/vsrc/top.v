module top(
    input        clk,    // 时钟
    input        rst,    // 复位，高电平有效
  output [7:0] led,    // 8 个 LED

  // 指令存储器接口
  output [31:0] imem_addr,
  input  [31:0] imem_rdata,

  // 数据存储器接口
  output        dmem_valid,
  output        dmem_we,
  output [3:0]  dmem_wmask,
  output [31:0] dmem_addr,
  output [31:0] dmem_wdata,
  input  [31:0] dmem_rdata,

  output        trap,
  output [31:0] trap_code,
  output [31:0] dbg_x1_o
);

/* verilator lint_off UNUSEDSIGNAL */

  // 实例化最小 RV32E minirv 处理器
  wire [31:0] dbg_pc;
  wire [31:0] dbg_x1;

  minirv_core u_core (
    .clk    (clk),
    .rst    (rst),
    .imem_addr (imem_addr),
    .imem_rdata(imem_rdata),
    .dmem_valid(dmem_valid),
    .dmem_we   (dmem_we),
    .dmem_wmask(dmem_wmask),
    .dmem_addr (dmem_addr),
    .dmem_wdata(dmem_wdata),
    .dmem_rdata(dmem_rdata),
    .trap      (trap),
    .trap_code (trap_code),
    .dbg_pc (dbg_pc),
    .dbg_x1 (dbg_x1)
  );

  // 将 PC 的低 8 位接到 LED 上
  assign led = dbg_pc[7:0];
  assign dbg_x1_o = dbg_x1;

endmodule

/* verilator lint_on UNUSEDSIGNAL */
