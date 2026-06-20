`include "minirv_defs.vh"
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
    input  wire [31:0] csr_read_data,
    input  wire [31:0] mtvec,
    input  wire [31:0] mepc,
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
    output reg  [31:0] trap_code,
    output reg  [11:0] csr_addr,
    output reg         csr_write_enable,
    output reg  [31:0] csr_write_data,
    output reg         ecall
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
  wire is_mret = (opcode == `OPCODE_SYSTEM) &&
                 (funct3 == 3'b000) &&
                 (rs1_raw == 5'd0) &&
                 (rd_raw == 5'd0) &&
                 (imm_i[11:0] == 12'h302);
  reg illegal_instr;

  always @(*) begin
    illegal_instr = 1'b0;

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
    csr_addr = imm_i[11:0];
    csr_write_enable = 1'b0;
    csr_write_data = 32'b0;
    ecall = 1'b0;

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
                illegal_instr = 1'b1;
              end
            end
            `F3_SLL: begin
              if (funct7 == 7'b0000000) begin
                wb_data_pre = rs1_val << shamt_r;
              end else begin
                illegal_instr = 1'b1;
              end
            end
            `F3_SLT: begin
              if (funct7 == 7'b0000000) begin
                wb_data_pre = {31'b0, rs1_signed < rs2_signed};
              end else begin
                illegal_instr = 1'b1;
              end
            end
            `F3_SLTU: begin
              if (funct7 == 7'b0000000) begin
                wb_data_pre = {31'b0, rs1_val < rs2_val};
              end else begin
                illegal_instr = 1'b1;
              end
            end
            `F3_XOR: begin
              if (funct7 == 7'b0000000) begin
                wb_data_pre = rs1_val ^ rs2_val;
              end else begin
                illegal_instr = 1'b1;
              end
            end
            `F3_SRL_SRA: begin
              if (funct7 == 7'b0100000) begin
                wb_data_pre = rs1_signed >>> shamt_r;
              end else if (funct7 == 7'b0000000) begin
                wb_data_pre = rs1_val >> shamt_r;
              end else begin
                illegal_instr = 1'b1;
              end
            end
            `F3_OR: begin
              if (funct7 == 7'b0000000) begin
                wb_data_pre = rs1_val | rs2_val;
              end else begin
                illegal_instr = 1'b1;
              end
            end
            `F3_AND: begin
              if (funct7 == 7'b0000000) begin
                wb_data_pre = rs1_val & rs2_val;
              end else begin
                illegal_instr = 1'b1;
              end
            end
            default: begin
              illegal_instr = 1'b1;
            end
          endcase
        end else begin
          illegal_instr = 1'b1;
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
                illegal_instr = 1'b1;
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
                illegal_instr = 1'b1;
              end
            end
            `F3_OR: wb_data_pre = rs1_val | imm_i;
            `F3_AND: wb_data_pre = rs1_val & imm_i;
            default: illegal_instr = 1'b1;
          endcase
        end else begin
          illegal_instr = 1'b1;
        end
      end

      `OPCODE_AUIPC: begin
        if (rd_raw < 5'd16) begin
          wb_en = (rd_idx != 4'd0);
          wb_data_pre = pc + imm_u;
        end else begin
          illegal_instr = 1'b1;
        end
      end

      `OPCODE_LUI: begin
        if (rd_raw < 5'd16) begin
          wb_en = (rd_idx != 4'd0);
          wb_data_pre = imm_u;
        end else begin
          illegal_instr = 1'b1;
        end
      end

      `OPCODE_JAL: begin
        if (rd_raw < 5'd16) begin
          wb_en = (rd_idx != 4'd0);
          wb_data_pre = pc_next_seq;
          pc_next = pc + imm_j;
        end else begin
          illegal_instr = 1'b1;
        end
      end

      `OPCODE_LOAD: begin
        if (rd_raw < 5'd16 && rs1_raw < 5'd16) begin
          case (funct3)
            `F3_LB, `F3_LH, `F3_LW, `F3_LBU, `F3_LHU: begin
              dmem_valid = 1'b1;
              dmem_we = 1'b0;
              dmem_addr = load_word_addr;
              wb_en = (rd_idx != 4'd0);
              wb_from_mem = 1'b1;
            end
            default: illegal_instr = 1'b1;
          endcase
        end else begin
          illegal_instr = 1'b1;
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
              illegal_instr = 1'b1;
            end
          endcase
        end else begin
          illegal_instr = 1'b1;
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
              illegal_instr = 1'b1;
            end
          endcase
        end else begin
          illegal_instr = 1'b1;
        end
      end

      `OPCODE_MISC_MEM: begin
        if (rd_raw == 5'd0 && rs1_raw == 5'd0 &&
            (funct3 == 3'b000 || funct3 == 3'b001)) begin
          pc_next = pc_next_seq;
        end else begin
          illegal_instr = 1'b1;
        end
      end

      `OPCODE_JALR: begin
        if (rd_raw < 5'd16 && rs1_raw < 5'd16 && funct3 == 3'b000) begin
          wb_en = (rd_idx != 4'd0);
          wb_data_pre = pc_next_seq;
          pc_next = (rs1_val + imm_i) & ~32'b1;
        end else begin
          illegal_instr = 1'b1;
        end
      end

      `OPCODE_SYSTEM: begin
        if (is_ebreak) begin
          trap = 1'b1;
          trap_code = a0_val;
          pc_next = pc;
        end else if (is_ecall) begin
          ecall = 1'b1;
          pc_next = mtvec;
        end else if (is_mret) begin
          pc_next = mepc;
        end else if ((funct3 == 3'b001 || funct3 == 3'b010) &&
                     rd_raw < 5'd16 && rs1_raw < 5'd16) begin
          wb_en = (rd_idx != 4'd0);
          wb_data_pre = csr_read_data;
          if (funct3 == 3'b001) begin
            csr_write_enable = 1'b1;
            csr_write_data = rs1_val;
          end else begin
            csr_write_enable = (rs1_raw != 5'd0);
            csr_write_data = csr_read_data | rs1_val;
          end
        end else begin
          illegal_instr = 1'b1;
        end
      end

      default: begin
        illegal_instr = 1'b1;
      end
    endcase

    if (illegal_instr) begin
      wb_en = 1'b0;
      wb_from_mem = 1'b0;
      dmem_valid = 1'b0;
      dmem_we = 1'b0;
      dmem_wmask = 4'b0000;
      csr_write_enable = 1'b0;
      ecall = 1'b0;
      trap = 1'b1;
      trap_code = 32'd2;
      pc_next = pc;
    end
  end
endmodule
