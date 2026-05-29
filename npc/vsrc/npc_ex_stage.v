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
    input  wire [31:0] imm_u,
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
        if (rd_raw < 5'd16 && rs1_raw < 5'd16 && rs2_raw < 5'd16 &&
            funct3 == `F3_ADD_SUB && funct7 == 7'b0000000) begin
          wb_en = (rd_idx != 4'd0);
          wb_data_pre = rs1_val + rs2_val;
        end
      end

      `OPCODE_OP_IMM: begin
        if (rd_raw < 5'd16 && rs1_raw < 5'd16 && funct3 == `F3_ADD_SUB) begin
          wb_en = (rd_idx != 4'd0);
          wb_data_pre = rs1_val + imm_i;
        end
      end

      `OPCODE_LUI: begin
        if (rd_raw < 5'd16) begin
          wb_en = (rd_idx != 4'd0);
          wb_data_pre = imm_u;
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
        end
      end

      default: begin
      end
    endcase
  end
endmodule
