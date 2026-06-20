module npc_if_stage(
    input  wire [31:0] pc,
    input  wire [31:0] imem_rdata,
    /* verilator lint_off UNUSEDSIGNAL */
    input  wire        out_ready,
    /* verilator lint_on UNUSEDSIGNAL */
    output wire        out_valid,
    output wire [31:0] imem_addr,
    output wire [31:0] instr,
    output wire [31:0] pc_next_seq
);
  assign out_valid = 1'b1;
  assign imem_addr = pc;
  assign instr = imem_rdata;
  assign pc_next_seq = pc + 32'd4;
endmodule
