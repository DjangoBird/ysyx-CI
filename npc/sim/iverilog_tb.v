`timescale 1ns / 1ps

module iverilog_tb;
  localparam MEM_WORDS = 32 * 1024 * 1024;
  localparam PMEM_BASE = 32'h8000_0000;
  localparam MAX_CYCLES = 2000000;

  reg clk = 1'b0;
  reg rst = 1'b1;
  reg commit_ready = 1'b1;
  wire [7:0] led;

  wire mem_axi_arvalid;
  reg mem_axi_arready = 1'b1;
  wire [31:0] mem_axi_araddr;
  reg mem_axi_rvalid = 1'b0;
  wire mem_axi_rready;
  reg [31:0] mem_axi_rdata = 32'b0;
  reg [1:0] mem_axi_rresp = 2'b0;
  wire mem_axi_awvalid;
  reg mem_axi_awready = 1'b1;
  wire [31:0] mem_axi_awaddr;
  wire mem_axi_wvalid;
  reg mem_axi_wready = 1'b1;
  wire [31:0] mem_axi_wdata;
  wire [3:0] mem_axi_wstrb;
  reg mem_axi_bvalid = 1'b0;
  wire mem_axi_bready;
  reg [1:0] mem_axi_bresp = 2'b0;

  wire trap;
  wire [31:0] trap_code;
  wire commit_valid;
  wire [31:0] commit_pc;
  wire [31:0] commit_instr;
  wire [31:0] commit_next_pc;
  wire commit_trap;
  wire [31:0] commit_trap_code;
  wire commit_mem_valid;
  wire commit_mem_we;
  wire [3:0] commit_mem_wmask;
  wire [31:0] commit_mem_addr;
  wire [31:0] commit_mem_wdata;
  wire [31:0] commit_mem_rdata;
  wire [31:0] dbg_pc_o;
  wire [31:0] dbg_x0_o;
  wire [31:0] dbg_x1_o;
  wire [31:0] dbg_x2_o;
  wire [31:0] dbg_x3_o;
  wire [31:0] dbg_x4_o;
  wire [31:0] dbg_x5_o;
  wire [31:0] dbg_x6_o;
  wire [31:0] dbg_x7_o;
  wire [31:0] dbg_x8_o;
  wire [31:0] dbg_x9_o;
  wire [31:0] dbg_x10_o;
  wire [31:0] dbg_x11_o;
  wire [31:0] dbg_x12_o;
  wire [31:0] dbg_x13_o;
  wire [31:0] dbg_x14_o;
  wire [31:0] dbg_x15_o;

  reg [31:0] mem [0:MEM_WORDS - 1];
  integer cycle;
  integer i;
  reg [31:0] raddr_q;
  reg [31:0] awaddr_q;
  reg aw_seen;
  reg [1023:0] mem_file;

  ysyx_26060168 dut (
    .clk(clk),
    .rst(rst),
    .commit_ready(commit_ready),
    .led(led),
    .mem_axi_arvalid(mem_axi_arvalid),
    .mem_axi_arready(mem_axi_arready),
    .mem_axi_araddr(mem_axi_araddr),
    .mem_axi_rvalid(mem_axi_rvalid),
    .mem_axi_rready(mem_axi_rready),
    .mem_axi_rdata(mem_axi_rdata),
    .mem_axi_rresp(mem_axi_rresp),
    .mem_axi_awvalid(mem_axi_awvalid),
    .mem_axi_awready(mem_axi_awready),
    .mem_axi_awaddr(mem_axi_awaddr),
    .mem_axi_wvalid(mem_axi_wvalid),
    .mem_axi_wready(mem_axi_wready),
    .mem_axi_wdata(mem_axi_wdata),
    .mem_axi_wstrb(mem_axi_wstrb),
    .mem_axi_bvalid(mem_axi_bvalid),
    .mem_axi_bready(mem_axi_bready),
    .mem_axi_bresp(mem_axi_bresp),
    .trap(trap),
    .trap_code(trap_code),
    .commit_valid(commit_valid),
    .commit_pc(commit_pc),
    .commit_instr(commit_instr),
    .commit_next_pc(commit_next_pc),
    .commit_trap(commit_trap),
    .commit_trap_code(commit_trap_code),
    .commit_mem_valid(commit_mem_valid),
    .commit_mem_we(commit_mem_we),
    .commit_mem_wmask(commit_mem_wmask),
    .commit_mem_addr(commit_mem_addr),
    .commit_mem_wdata(commit_mem_wdata),
    .commit_mem_rdata(commit_mem_rdata),
    .dbg_pc_o(dbg_pc_o),
    .dbg_x0_o(dbg_x0_o),
    .dbg_x1_o(dbg_x1_o),
    .dbg_x2_o(dbg_x2_o),
    .dbg_x3_o(dbg_x3_o),
    .dbg_x4_o(dbg_x4_o),
    .dbg_x5_o(dbg_x5_o),
    .dbg_x6_o(dbg_x6_o),
    .dbg_x7_o(dbg_x7_o),
    .dbg_x8_o(dbg_x8_o),
    .dbg_x9_o(dbg_x9_o),
    .dbg_x10_o(dbg_x10_o),
    .dbg_x11_o(dbg_x11_o),
    .dbg_x12_o(dbg_x12_o),
    .dbg_x13_o(dbg_x13_o),
    .dbg_x14_o(dbg_x14_o),
    .dbg_x15_o(dbg_x15_o)
  );

  always #5 clk = ~clk;

  function [31:0] word_index;
    input [31:0] addr;
    begin
      word_index = (addr - PMEM_BASE) >> 2;
    end
  endfunction

  task write_word;
    input [31:0] addr;
    input [31:0] data;
    input [3:0] strb;
    reg [31:0] idx;
    begin
      idx = word_index(addr);
      if (idx < MEM_WORDS) begin
        if (strb[0]) mem[idx][7:0] = data[7:0];
        if (strb[1]) mem[idx][15:8] = data[15:8];
        if (strb[2]) mem[idx][23:16] = data[23:16];
        if (strb[3]) mem[idx][31:24] = data[31:24];
      end
    end
  endtask

  initial begin
    for (i = 0; i < MEM_WORDS; i = i + 1) begin
      mem[i] = 32'b0;
    end
    if (!$value$plusargs("MEM=%s", mem_file)) begin
      $display("missing +MEM=<hex-file>");
      $finish;
    end
    $readmemh(mem_file, mem);
    repeat (8) @(posedge clk);
    rst = 1'b0;
  end

  always @(posedge clk) begin
    if (rst) begin
      cycle <= 0;
      mem_axi_rvalid <= 1'b0;
      mem_axi_bvalid <= 1'b0;
      aw_seen <= 1'b0;
    end else begin
      cycle <= cycle + 1;

      if (mem_axi_arvalid && mem_axi_arready) begin
        raddr_q <= mem_axi_araddr;
        mem_axi_rdata <= mem[word_index(mem_axi_araddr)];
        mem_axi_rresp <= 2'b0;
        mem_axi_rvalid <= 1'b1;
      end else if (mem_axi_rvalid && mem_axi_rready) begin
        mem_axi_rvalid <= 1'b0;
      end

      if (mem_axi_awvalid && mem_axi_awready) begin
        awaddr_q <= mem_axi_awaddr;
        aw_seen <= 1'b1;
      end
      if (mem_axi_wvalid && mem_axi_wready && aw_seen) begin
        write_word(awaddr_q, mem_axi_wdata, mem_axi_wstrb);
        mem_axi_bresp <= 2'b0;
        mem_axi_bvalid <= 1'b1;
        aw_seen <= 1'b0;
      end else if (mem_axi_bvalid && mem_axi_bready) begin
        mem_axi_bvalid <= 1'b0;
      end

      if (trap) begin
        if (trap_code == 32'b0) begin
          $display("HIT GOOD TRAP");
          $finish;
        end else begin
          $display("HIT BAD TRAP: %08x", trap_code);
          $fatal;
        end
      end

      if (cycle > MAX_CYCLES) begin
        $display("simulation timeout");
        $fatal;
      end
    end
  end
endmodule
