`include "minirv_defs.vh"

module npc_id_stage(
    input  wire        in_valid,
    output wire        in_ready,
    output wire        out_valid,
    input  wire        out_ready,
    input  wire [31:0] instr,
    output wire [6:0]  opcode,
    output wire [4:0]  rd_raw,
    output wire [2:0]  funct3,
    output wire [4:0]  rs1_raw,
    output wire [4:0]  rs2_raw,
    output wire [6:0]  funct7,
    output wire [3:0]  rd_idx,
    output wire [3:0]  rs1_idx,
    output wire [3:0]  rs2_idx,
    output wire [31:0] imm_i,
    output wire [31:0] imm_s,
    output wire [31:0] imm_b,
    output wire [31:0] imm_u,
    output wire [31:0] imm_j,
    output wire        is_ebreak
);
  assign in_ready = out_ready;
  assign out_valid = in_valid;

  assign opcode = instr[6:0];
  assign rd_raw = instr[11:7];
  assign funct3 = instr[14:12];
  assign rs1_raw = instr[19:15];
  assign rs2_raw = instr[24:20];
  assign funct7 = instr[31:25];

  assign rd_idx = rd_raw[3:0];
  assign rs1_idx = rs1_raw[3:0];
  assign rs2_idx = rs2_raw[3:0];

  assign imm_i = {{20{instr[31]}}, instr[31:20]};
  assign imm_s = {{20{instr[31]}}, instr[31:25], instr[11:7]};
  assign imm_b = {{19{instr[31]}}, instr[31], instr[7], instr[30:25],
                  instr[11:8], 1'b0};
  assign imm_u = {instr[31:12], 12'b0};
  assign imm_j = {{11{instr[31]}}, instr[31], instr[19:12], instr[20],
                  instr[30:21], 1'b0};

  assign is_ebreak = (opcode == `OPCODE_SYSTEM) &&
                     (funct3 == 3'b000) &&
                     (rs1_raw == 5'd0) &&
                     (rd_raw == 5'd0) &&
                     (instr[31:20] == 12'h001);
endmodule
