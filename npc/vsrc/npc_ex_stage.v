`include "minirv_defs.vh"
import "DPI-C" function void npc_ebreak(input int pc, input int code);

module npc_ex_stage(
    input  wire [6:0]  opcode,
    input  wire [4:0]  rd_raw,
    input  wire [2:0]  funct3,
    input  wire [4:0]  rs1_raw,
    input  wire [4:0]  rs2_raw,
    input  wire [6:0]  funct7,
    input  wire [3:0]  rd_idx,
    input  wire [31:0] rs1_val,
    input  wire [31:0] rs2_val,
    input  wire [31:0] imm_i,
    input  wire [31:0] imm_s,
    input  wire [31:0] imm_b,
    input  wire [31:0] imm_u,
    input  wire [31:0] imm_j,
    input  wire [31:0] a0_val,
    input  wire [31:0] pc,
    input  wire [31:0] pc_next_seq,
    input  wire        is_ebreak,
    output reg         wb_en,
    output reg  [3:0]  wb_idx,
    output reg  [31:0] wb_data_pre,
    output reg         wb_from_mem,
    output reg         dmem_valid,
    output reg         dmem_we,
    output reg  [3:0]  dmem_wmask,
    output reg  [31:0] dmem_addr,
    output reg  [31:0] dmem_wdata,
    output reg  [1:0]  load_byte_off,
    output reg  [2:0]  load_funct3,
    output reg  [31:0] pc_next,
    output reg         trap,
    output reg  [31:0] trap_code
);
  wire [31:0] load_addr = rs1_val + imm_i;
  wire [31:0] store_addr = rs1_val + imm_s;
  wire [31:0] load_word_addr = {load_addr[31:2], 2'b00};
  wire [31:0] store_word_addr = {store_addr[31:2], 2'b00};
  wire [4:0] shamt_i = imm_i[4:0];
  wire [4:0] shamt_r = rs2_val[4:0];
  wire signed [31:0] rs1_signed = rs1_val;
  wire signed [31:0] rs2_signed = rs2_val;
  wire is_ecall = (opcode == `OPCODE_SYSTEM) &&
                  (funct3 == 3'b000) &&
                  (rs1_raw == 5'd0) &&
                  (rd_raw == 5'd0) &&
                  (imm_i[11:0] == 12'h000);

  always @(*) begin
    wb_en = 1'b0;
    wb_idx = rd_idx;
    wb_data_pre = 32'b0;
    wb_from_mem = 1'b0;

    dmem_valid = 1'b0;
    dmem_we = 1'b0;
    dmem_wmask = 4'b0000;
    dmem_addr = 32'b0;
    dmem_wdata = 32'b0;
    load_byte_off = load_addr[1:0];
    load_funct3 = funct3;

    pc_next = pc_next_seq;
    trap = 1'b0;
    trap_code = 32'b0;

    case (opcode)
      `OPCODE_OP: begin
        if (rd_raw < 5'd16 && rs1_raw < 5'd16 && rs2_raw < 5'd16) begin
          wb_en = (rd_idx != 4'd0);
          case (funct3)
            `F3_ADD_SUB: begin
              if (funct7 == 7'b0100000) begin
                wb_data_pre = rs1_val - rs2_val;
              end else if (funct7 == 7'b0000000) begin
                wb_data_pre = rs1_val + rs2_val;
              end else begin
                wb_en = 1'b0;
              end
            end
            `F3_SLL: begin
              if (funct7 == 7'b0000000) begin
                wb_data_pre = rs1_val << shamt_r;
              end else begin
                wb_en = 1'b0;
              end
            end
            `F3_SLT: begin
              if (funct7 == 7'b0000000) begin
                wb_data_pre = {31'b0, rs1_signed < rs2_signed};
              end else begin
                wb_en = 1'b0;
              end
            end
            `F3_SLTU: begin
              if (funct7 == 7'b0000000) begin
                wb_data_pre = {31'b0, rs1_val < rs2_val};
              end else begin
                wb_en = 1'b0;
              end
            end
            `F3_XOR: begin
              if (funct7 == 7'b0000000) begin
                wb_data_pre = rs1_val ^ rs2_val;
              end else begin
                wb_en = 1'b0;
              end
            end
            `F3_SRL_SRA: begin
              if (funct7 == 7'b0100000) begin
                wb_data_pre = rs1_signed >>> shamt_r;
              end else if (funct7 == 7'b0000000) begin
                wb_data_pre = rs1_val >> shamt_r;
              end else begin
                wb_en = 1'b0;
              end
            end
            `F3_OR: begin
              if (funct7 == 7'b0000000) begin
                wb_data_pre = rs1_val | rs2_val;
              end else begin
                wb_en = 1'b0;
              end
            end
            `F3_AND: begin
              if (funct7 == 7'b0000000) begin
                wb_data_pre = rs1_val & rs2_val;
              end else begin
                wb_en = 1'b0;
              end
            end
            default: begin
              wb_en = 1'b0;
            end
          endcase
        end
      end

      `OPCODE_OP_IMM: begin
        if (rd_raw < 5'd16 && rs1_raw < 5'd16) begin
          wb_en = (rd_idx != 4'd0);
          case (funct3)
            `F3_ADD_SUB: wb_data_pre = rs1_val + imm_i;
            `F3_SLL: begin
              if (imm_i[11:5] == 7'b0000000) begin
                wb_data_pre = rs1_val << shamt_i;
              end else begin
                wb_en = 1'b0;
              end
            end
            `F3_SLT: wb_data_pre = {31'b0, rs1_signed < $signed(imm_i)};
            `F3_SLTU: wb_data_pre = {31'b0, rs1_val < imm_i};
            `F3_XOR: wb_data_pre = rs1_val ^ imm_i;
            `F3_SRL_SRA: begin
              if (imm_i[11:5] == 7'b0100000) begin
                wb_data_pre = rs1_signed >>> shamt_i;
              end else if (imm_i[11:5] == 7'b0000000) begin
                wb_data_pre = rs1_val >> shamt_i;
              end else begin
                wb_en = 1'b0;
              end
            end
            `F3_OR: wb_data_pre = rs1_val | imm_i;
            `F3_AND: wb_data_pre = rs1_val & imm_i;
            default: wb_en = 1'b0;
          endcase
        end
      end

      `OPCODE_AUIPC: begin
        if (rd_raw < 5'd16) begin
          wb_en = (rd_idx != 4'd0);
          wb_data_pre = pc + imm_u;
        end
      end

      `OPCODE_LUI: begin
        if (rd_raw < 5'd16) begin
          wb_en = (rd_idx != 4'd0);
          wb_data_pre = imm_u;
        end
      end

      `OPCODE_JAL: begin
        if (rd_raw < 5'd16) begin
          wb_en = (rd_idx != 4'd0);
          wb_data_pre = pc_next_seq;
          pc_next = pc + imm_j;
        end
      end

      `OPCODE_LOAD: begin
        if (rd_raw < 5'd16 && rs1_raw < 5'd16) begin
          dmem_valid = 1'b1;
          dmem_we = 1'b0;
          dmem_addr = load_word_addr;
          wb_en = (rd_idx != 4'd0);
          wb_from_mem = 1'b1;
        end
      end

      `OPCODE_STORE: begin
        if (rs1_raw < 5'd16 && rs2_raw < 5'd16) begin
          dmem_valid = 1'b1;
          dmem_we = 1'b1;
          dmem_addr = store_word_addr;
          case (funct3)
            `F3_SW: begin
              dmem_wmask = 4'b1111;
              dmem_wdata = rs2_val;
            end
            `F3_SH: begin
              if (store_addr[1]) begin
                dmem_wmask = 4'b1100;
                dmem_wdata = {rs2_val[15:0], 16'b0};
              end else begin
                dmem_wmask = 4'b0011;
                dmem_wdata = {16'b0, rs2_val[15:0]};
              end
            end
            `F3_SB: begin
              case (store_addr[1:0])
                2'b00: begin
                  dmem_wmask = 4'b0001;
                  dmem_wdata = {24'b0, rs2_val[7:0]};
                end
                2'b01: begin
                  dmem_wmask = 4'b0010;
                  dmem_wdata = {16'b0, rs2_val[7:0], 8'b0};
                end
                2'b10: begin
                  dmem_wmask = 4'b0100;
                  dmem_wdata = {8'b0, rs2_val[7:0], 16'b0};
                end
                default: begin
                  dmem_wmask = 4'b1000;
                  dmem_wdata = {rs2_val[7:0], 24'b0};
                end
              endcase
            end
            default: begin
              dmem_valid = 1'b0;
              dmem_we = 1'b0;
              dmem_wmask = 4'b0000;
            end
          endcase
        end
      end

      `OPCODE_BRANCH: begin
        if (rs1_raw < 5'd16 && rs2_raw < 5'd16) begin
          case (funct3)
            `F3_BEQ: begin
              if (rs1_val == rs2_val) pc_next = pc + imm_b;
            end
            `F3_BNE: begin
              if (rs1_val != rs2_val) pc_next = pc + imm_b;
            end
            `F3_BLT: begin
              if (rs1_signed < rs2_signed) pc_next = pc + imm_b;
            end
            `F3_BGE: begin
              if (rs1_signed >= rs2_signed) pc_next = pc + imm_b;
            end
            `F3_BLTU: begin
              if (rs1_val < rs2_val) pc_next = pc + imm_b;
            end
            `F3_BGEU: begin
              if (rs1_val >= rs2_val) pc_next = pc + imm_b;
            end
            default: begin
            end
          endcase
        end
      end

      `OPCODE_MISC_MEM: begin
        if (rd_raw == 5'd0 && rs1_raw == 5'd0) begin
          pc_next = pc_next_seq;
        end
      end

      `OPCODE_JALR: begin
        if (rd_raw < 5'd16 && rs1_raw < 5'd16 && funct3 == 3'b000) begin
          wb_en = (rd_idx != 4'd0);
          wb_data_pre = pc_next_seq;
          pc_next = (rs1_val + imm_i) & ~32'b1;
        end
      end

      `OPCODE_SYSTEM: begin
        if (is_ebreak) begin
          trap = 1'b1;
          trap_code = a0_val;
          npc_ebreak(pc, a0_val);
          pc_next = pc;
        end else if (is_ecall) begin
          trap = 1'b1;
          trap_code = 32'd1;
          pc_next = pc;
        end
      end

      default: begin
      end
    endcase
  end
endmodule
