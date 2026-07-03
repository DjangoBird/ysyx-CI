module top (
    input         clk,
    input         rst,
    input         commit_ready,
    output [7:0]  led,

    output        mem_axi_arvalid,
    input         mem_axi_arready,
    output [31:0] mem_axi_araddr,
    input         mem_axi_rvalid,
    output        mem_axi_rready,
    input  [31:0] mem_axi_rdata,
    input  [1:0]  mem_axi_rresp,
    output        mem_axi_awvalid,
    input         mem_axi_awready,
    output [31:0] mem_axi_awaddr,
    output        mem_axi_wvalid,
    input         mem_axi_wready,
    output [31:0] mem_axi_wdata,
    output [3:0]  mem_axi_wstrb,
    input         mem_axi_bvalid,
    output        mem_axi_bready,
    input  [1:0]  mem_axi_bresp,

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
    output [31:0] dbg_pc_o,
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
  wire ifu_arvalid;
  wire ifu_arready;
  wire [31:0] ifu_araddr;
  wire ifu_rvalid;
  wire ifu_rready;
  wire [31:0] ifu_rdata;
  wire [1:0] ifu_rresp;

  wire lsu_arvalid;
  wire lsu_arready;
  wire [31:0] lsu_araddr;
  wire lsu_rvalid;
  wire lsu_rready;
  wire [31:0] lsu_rdata;
  wire [1:0] lsu_rresp;
  wire lsu_awvalid;
  wire lsu_awready;
  wire [31:0] lsu_awaddr;
  wire lsu_wvalid;
  wire lsu_wready;
  wire [31:0] lsu_wdata;
  wire [3:0] lsu_wstrb;
  wire lsu_bvalid;
  wire lsu_bready;
  wire [1:0] lsu_bresp;

  wire bus_arvalid;
  wire bus_arready;
  wire [31:0] bus_araddr;
  wire bus_rvalid;
  wire bus_rready;
  wire [31:0] bus_rdata;
  wire [1:0] bus_rresp;
  wire bus_awvalid;
  wire bus_awready;
  wire [31:0] bus_awaddr;
  wire bus_wvalid;
  wire bus_wready;
  wire [31:0] bus_wdata;
  wire [3:0] bus_wstrb;
  wire bus_bvalid;
  wire bus_bready;
  wire [1:0] bus_bresp;

  wire uart_awvalid;
  wire uart_awready;
  wire [31:0] uart_awaddr;
  wire uart_wvalid;
  wire uart_wready;
  wire [31:0] uart_wdata;
  wire [3:0] uart_wstrb;
  wire uart_bvalid;
  wire uart_bready;
  wire [1:0] uart_bresp;

  wire clint_arvalid;
  wire clint_arready;
  wire [31:0] clint_araddr;
  wire clint_rvalid;
  wire clint_rready;
  wire [31:0] clint_rdata;
  wire [1:0] clint_rresp;

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
    .clk(clk),
    .rst(rst),
    .commit_ready(commit_ready),
    .ifu_axi_arvalid(ifu_arvalid),
    .ifu_axi_arready(ifu_arready),
    .ifu_axi_araddr(ifu_araddr),
    .ifu_axi_rvalid(ifu_rvalid),
    .ifu_axi_rready(ifu_rready),
    .ifu_axi_rdata(ifu_rdata),
    .ifu_axi_rresp(ifu_rresp),
    .lsu_axi_arvalid(lsu_arvalid),
    .lsu_axi_arready(lsu_arready),
    .lsu_axi_araddr(lsu_araddr),
    .lsu_axi_rvalid(lsu_rvalid),
    .lsu_axi_rready(lsu_rready),
    .lsu_axi_rdata(lsu_rdata),
    .lsu_axi_rresp(lsu_rresp),
    .lsu_axi_awvalid(lsu_awvalid),
    .lsu_axi_awready(lsu_awready),
    .lsu_axi_awaddr(lsu_awaddr),
    .lsu_axi_wvalid(lsu_wvalid),
    .lsu_axi_wready(lsu_wready),
    .lsu_axi_wdata(lsu_wdata),
    .lsu_axi_wstrb(lsu_wstrb),
    .lsu_axi_bvalid(lsu_bvalid),
    .lsu_axi_bready(lsu_bready),
    .lsu_axi_bresp(lsu_bresp),
    .trap(core_trap),
    .trap_code(core_trap_code),
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
    .dbg_pc(dbg_pc),
    .dbg_x0(dbg_x0),
    .dbg_x1(dbg_x1),
    .dbg_x2(dbg_x2),
    .dbg_x3(dbg_x3),
    .dbg_x4(dbg_x4),
    .dbg_x5(dbg_x5),
    .dbg_x6(dbg_x6),
    .dbg_x7(dbg_x7),
    .dbg_x8(dbg_x8),
    .dbg_x9(dbg_x9),
    .dbg_x10(dbg_x10),
    .dbg_x11(dbg_x11),
    .dbg_x12(dbg_x12),
    .dbg_x13(dbg_x13),
    .dbg_x14(dbg_x14),
    .dbg_x15(dbg_x15)
  );

  npc_axi_arbiter u_arbiter (
    .clk(clk),
    .rst(rst),
    .ifu_arvalid(ifu_arvalid),
    .ifu_arready(ifu_arready),
    .ifu_araddr(ifu_araddr),
    .ifu_rvalid(ifu_rvalid),
    .ifu_rready(ifu_rready),
    .ifu_rdata(ifu_rdata),
    .ifu_rresp(ifu_rresp),
    .lsu_arvalid(lsu_arvalid),
    .lsu_arready(lsu_arready),
    .lsu_araddr(lsu_araddr),
    .lsu_rvalid(lsu_rvalid),
    .lsu_rready(lsu_rready),
    .lsu_rdata(lsu_rdata),
    .lsu_rresp(lsu_rresp),
    .lsu_awvalid(lsu_awvalid),
    .lsu_awready(lsu_awready),
    .lsu_awaddr(lsu_awaddr),
    .lsu_wvalid(lsu_wvalid),
    .lsu_wready(lsu_wready),
    .lsu_wdata(lsu_wdata),
    .lsu_wstrb(lsu_wstrb),
    .lsu_bvalid(lsu_bvalid),
    .lsu_bready(lsu_bready),
    .lsu_bresp(lsu_bresp),
    .out_arvalid(bus_arvalid),
    .out_arready(bus_arready),
    .out_araddr(bus_araddr),
    .out_rvalid(bus_rvalid),
    .out_rready(bus_rready),
    .out_rdata(bus_rdata),
    .out_rresp(bus_rresp),
    .out_awvalid(bus_awvalid),
    .out_awready(bus_awready),
    .out_awaddr(bus_awaddr),
    .out_wvalid(bus_wvalid),
    .out_wready(bus_wready),
    .out_wdata(bus_wdata),
    .out_wstrb(bus_wstrb),
    .out_bvalid(bus_bvalid),
    .out_bready(bus_bready),
    .out_bresp(bus_bresp)
  );

  npc_axi_xbar u_xbar (
    .clk(clk),
    .rst(rst),
    .in_arvalid(bus_arvalid),
    .in_arready(bus_arready),
    .in_araddr(bus_araddr),
    .in_rvalid(bus_rvalid),
    .in_rready(bus_rready),
    .in_rdata(bus_rdata),
    .in_rresp(bus_rresp),
    .in_awvalid(bus_awvalid),
    .in_awready(bus_awready),
    .in_awaddr(bus_awaddr),
    .in_wvalid(bus_wvalid),
    .in_wready(bus_wready),
    .in_wdata(bus_wdata),
    .in_wstrb(bus_wstrb),
    .in_bvalid(bus_bvalid),
    .in_bready(bus_bready),
    .in_bresp(bus_bresp),
    .mem_arvalid(mem_axi_arvalid),
    .mem_arready(mem_axi_arready),
    .mem_araddr(mem_axi_araddr),
    .mem_rvalid(mem_axi_rvalid),
    .mem_rready(mem_axi_rready),
    .mem_rdata(mem_axi_rdata),
    .mem_rresp(mem_axi_rresp),
    .mem_awvalid(mem_axi_awvalid),
    .mem_awready(mem_axi_awready),
    .mem_awaddr(mem_axi_awaddr),
    .mem_wvalid(mem_axi_wvalid),
    .mem_wready(mem_axi_wready),
    .mem_wdata(mem_axi_wdata),
    .mem_wstrb(mem_axi_wstrb),
    .mem_bvalid(mem_axi_bvalid),
    .mem_bready(mem_axi_bready),
    .mem_bresp(mem_axi_bresp),
    .uart_awvalid(uart_awvalid),
    .uart_awready(uart_awready),
    .uart_awaddr(uart_awaddr),
    .uart_wvalid(uart_wvalid),
    .uart_wready(uart_wready),
    .uart_wdata(uart_wdata),
    .uart_wstrb(uart_wstrb),
    .uart_bvalid(uart_bvalid),
    .uart_bready(uart_bready),
    .uart_bresp(uart_bresp),
    .clint_arvalid(clint_arvalid),
    .clint_arready(clint_arready),
    .clint_araddr(clint_araddr),
    .clint_rvalid(clint_rvalid),
    .clint_rready(clint_rready),
    .clint_rdata(clint_rdata),
    .clint_rresp(clint_rresp)
  );

  npc_axi_uart u_uart (
    .clk(clk),
    .rst(rst),
    .awvalid(uart_awvalid),
    .awready(uart_awready),
    .awaddr(uart_awaddr),
    .wvalid(uart_wvalid),
    .wready(uart_wready),
    .wdata(uart_wdata),
    .wstrb(uart_wstrb),
    .bvalid(uart_bvalid),
    .bready(uart_bready),
    .bresp(uart_bresp)
  );

  npc_axi_clint u_clint (
    .clk(clk),
    .rst(rst),
    .arvalid(clint_arvalid),
    .arready(clint_arready),
    .araddr(clint_araddr),
    .rvalid(clint_rvalid),
    .rready(clint_rready),
    .rdata(clint_rdata),
    .rresp(clint_rresp)
  );

  assign led = dbg_pc[7:0];
  assign dbg_pc_o = dbg_pc;
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
