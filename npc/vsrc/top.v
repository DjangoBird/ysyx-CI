module top(
    input        clk,    // 时钟
    input        rst,    // 复位，高电平有效
  input        commit_ready,
  output [7:0] led,    // 8 个 LED

  // 指令存储器接口
  output [31:0] imem_addr,
  output        imem_req_valid,
  input         imem_req_ready,
  input         imem_resp_valid,
  output        imem_resp_ready,
  input  [31:0] imem_rdata,

  // 数据存储器接口
  output        dmem_valid,
  input         dmem_req_ready,
  input         dmem_resp_valid,
  output        dmem_resp_ready,
  output        dmem_we,
  output [3:0]  dmem_wmask,
  output [31:0] dmem_addr,
  output [31:0] dmem_wdata,
  input  [31:0] dmem_rdata,

  output reg        trap,
  output reg [31:0] trap_code,
  output        commit_valid,
  output [31:0] commit_pc,
  output [31:0] commit_instr,
  output [31:0] commit_next_pc,
  output        commit_trap,
  output [31:0] commit_trap_code,
  output        commit_mem_valid,
  output        commit_mem_we,
  output [3:0]  commit_mem_wmask,
  output [31:0] commit_mem_addr,
  output [31:0] commit_mem_wdata,
  output [31:0] commit_mem_rdata,
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
  wire core_trap;
  wire [31:0] core_trap_code;
  assign commit_trap = commit_valid && core_trap;
  assign commit_trap_code = core_trap_code;

  always @(posedge clk or posedge rst) begin
    if (rst) begin
      trap <= 1'b0;
      trap_code <= 32'b0;
    end else if (commit_valid && core_trap) begin
      trap <= 1'b1;
      trap_code <= core_trap_code;
    end
  end

  minirv_core u_core (
    .clk    (clk),
    .rst    (rst),
    .commit_ready(commit_ready),
    .imem_addr (imem_addr),
    .imem_req_valid(imem_req_valid),
    .imem_req_ready(imem_req_ready),
    .imem_resp_valid(imem_resp_valid),
    .imem_resp_ready(imem_resp_ready),
    .imem_rdata(imem_rdata),
    .dmem_valid(dmem_valid),
    .dmem_req_ready(dmem_req_ready),
    .dmem_resp_valid(dmem_resp_valid),
    .dmem_resp_ready(dmem_resp_ready),
    .dmem_we   (dmem_we),
    .dmem_wmask(dmem_wmask),
    .dmem_addr (dmem_addr),
    .dmem_wdata(dmem_wdata),
    .dmem_rdata(dmem_rdata),
    .trap      (core_trap),
    .trap_code (core_trap_code),
    .commit_valid(commit_valid),
    .commit_pc(commit_pc),
    .commit_instr(commit_instr),
    .commit_next_pc(commit_next_pc),
    .commit_mem_valid(commit_mem_valid),
    .commit_mem_we(commit_mem_we),
    .commit_mem_wmask(commit_mem_wmask),
    .commit_mem_addr(commit_mem_addr),
    .commit_mem_wdata(commit_mem_wdata),
    .commit_mem_rdata(commit_mem_rdata),
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
