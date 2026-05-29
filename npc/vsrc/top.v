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
  output [31:0] dbg_x0_o,
  output [31:0] dbg_x1_o,
  output [31:0] dbg_x2_o,
  output [31:0] dbg_x3_o,
  output [31:0] dbg_x4_o,
  output [31:0] dbg_x5_o,
  output [31:0] dbg_x6_o,
  output [31:0] dbg_x7_o,
  output [31:0] dbg_x8_o,
  output [31:0] dbg_x9_o,
  output [31:0] dbg_x10_o,
  output [31:0] dbg_x11_o,
  output [31:0] dbg_x12_o,
  output [31:0] dbg_x13_o,
  output [31:0] dbg_x14_o,
  output [31:0] dbg_x15_o
);

/* verilator lint_off UNUSEDSIGNAL */

  // 实例化最小 RV32E minirv 处理器
  wire [31:0] dbg_pc;
  wire [31:0] dbg_x0;
  wire [31:0] dbg_x1;
  wire [31:0] dbg_x2;
  wire [31:0] dbg_x3;
  wire [31:0] dbg_x4;
  wire [31:0] dbg_x5;
  wire [31:0] dbg_x6;
  wire [31:0] dbg_x7;
  wire [31:0] dbg_x8;
  wire [31:0] dbg_x9;
  wire [31:0] dbg_x10;
  wire [31:0] dbg_x11;
  wire [31:0] dbg_x12;
  wire [31:0] dbg_x13;
  wire [31:0] dbg_x14;
  wire [31:0] dbg_x15;

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
    .dbg_x0 (dbg_x0),
    .dbg_x1 (dbg_x1)
    ,.dbg_x2 (dbg_x2)
    ,.dbg_x3 (dbg_x3)
    ,.dbg_x4 (dbg_x4)
    ,.dbg_x5 (dbg_x5)
    ,.dbg_x6 (dbg_x6)
    ,.dbg_x7 (dbg_x7)
    ,.dbg_x8 (dbg_x8)
    ,.dbg_x9 (dbg_x9)
    ,.dbg_x10 (dbg_x10)
    ,.dbg_x11 (dbg_x11)
    ,.dbg_x12 (dbg_x12)
    ,.dbg_x13 (dbg_x13)
    ,.dbg_x14 (dbg_x14)
    ,.dbg_x15 (dbg_x15)
  );

  // 将 PC 的低 8 位接到 LED 上
  assign led = dbg_pc[7:0];
  assign dbg_x0_o = dbg_x0;
  assign dbg_x1_o = dbg_x1;
  assign dbg_x2_o = dbg_x2;
  assign dbg_x3_o = dbg_x3;
  assign dbg_x4_o = dbg_x4;
  assign dbg_x5_o = dbg_x5;
  assign dbg_x6_o = dbg_x6;
  assign dbg_x7_o = dbg_x7;
  assign dbg_x8_o = dbg_x8;
  assign dbg_x9_o = dbg_x9;
  assign dbg_x10_o = dbg_x10;
  assign dbg_x11_o = dbg_x11;
  assign dbg_x12_o = dbg_x12;
  assign dbg_x13_o = dbg_x13;
  assign dbg_x14_o = dbg_x14;
  assign dbg_x15_o = dbg_x15;

endmodule

/* verilator lint_on UNUSEDSIGNAL */
