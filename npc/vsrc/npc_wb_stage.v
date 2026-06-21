module npc_wb_stage(
    input  wire        clk,
    input  wire        rst,
    input  wire        commit_ready,
    input  wire        in_valid,
    output wire        in_ready,
    input  wire [31:0] pc_next,
    input  wire        wb_en,
    input  wire [3:0]  wb_idx,
    input  wire [31:0] wb_data,
    output wire [31:0] pc,
    output wire [31:0] dbg_x0,
    output wire [31:0] dbg_x1,
    output wire [31:0] dbg_x2,
    output wire [31:0] dbg_x3,
    output wire [31:0] dbg_x4,
    output wire [31:0] dbg_x5,
    output wire [31:0] dbg_x6,
    output wire [31:0] dbg_x7,
    output wire [31:0] dbg_x8,
    output wire [31:0] dbg_x9,
    output wire [31:0] dbg_x10,
    output wire [31:0] dbg_x11,
    output wire [31:0] dbg_x12,
    output wire [31:0] dbg_x13,
    output wire [31:0] dbg_x14,
    output wire [31:0] dbg_x15
);
  assign in_ready = commit_ready;

  reg [31:0] pc_reg;
  reg [31:0] regs[0:15];

  integer i;

  always @(posedge clk or posedge rst) begin
    if (rst) begin
      pc_reg <= 32'h8000_0000;
      for (i = 0; i < 16; i = i + 1) begin
        regs[i] <= 32'b0;
      end
    end else if (in_valid && in_ready) begin
      pc_reg <= pc_next;
      if (wb_en && wb_idx != 4'd0) begin
        regs[wb_idx] <= wb_data;
      end
    end
  end

  assign pc = pc_reg;
  assign dbg_x0 = regs[0];
  assign dbg_x1 = regs[1];
  assign dbg_x2 = regs[2];
  assign dbg_x3 = regs[3];
  assign dbg_x4 = regs[4];
  assign dbg_x5 = regs[5];
  assign dbg_x6 = regs[6];
  assign dbg_x7 = regs[7];
  assign dbg_x8 = regs[8];
  assign dbg_x9 = regs[9];
  assign dbg_x10 = regs[10];
  assign dbg_x11 = regs[11];
  assign dbg_x12 = regs[12];
  assign dbg_x13 = regs[13];
  assign dbg_x14 = regs[14];
  assign dbg_x15 = regs[15];
endmodule
