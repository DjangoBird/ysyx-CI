// Minimal single-cycle RV32E (minirv) core
// PC initial value = 0x80000000
// 16 GPRs (x0-x15), x0 hard-wired to 0
// Supported instructions: add, addi, lui, lw, lbu, sw, sb, jalr, ebreak
// Other ISA details follow RV32I

import "DPI-C" function void npc_ebreak(input int pc, input int code);

/* verilator lint_off UNOPTFLAT */

module minirv_core (
    input  wire         clk,
    input  wire         rst,

  // instruction memory interface
  output wire [31:0]  imem_addr,
  input  wire [31:0]  imem_rdata,

  // data memory interface
  output reg          dmem_valid,
  output reg          dmem_we,
  output reg  [3:0]   dmem_wmask,
  output reg  [31:0]  dmem_addr,
  output reg  [31:0]  dmem_wdata,
  input  wire [31:0]  dmem_rdata,

  // trap interface
  output reg          trap,
  output reg  [31:0]  trap_code,

  // debug observation port
  output wire [31:0]  dbg_pc,
  output wire [31:0]  dbg_x1
);

  // -------------------------------
  // PC & Register File (RV32E: 16 regs)
  // -------------------------------
  reg [31:0] pc;
  reg [31:0] regs[0:15];

  integer i;

  // -------------------------------
  // Instruction Fetch
  // -------------------------------
  wire [31:0] instr;
  wire [31:0] pc_next_seq = pc + 32'd4;

  assign imem_addr = pc;
  assign instr = imem_rdata;

  // -------------------------------
  // Decode fields (RV32I encoding)
  // -------------------------------
  wire [6:0] opcode = instr[6:0];
  wire [4:0] rd_raw = instr[11:7];
  wire [2:0] funct3 = instr[14:12];
  wire [4:0] rs1_raw = instr[19:15];
  wire [4:0] rs2_raw = instr[24:20];
  wire [6:0] funct7 = instr[31:25];

  // RV32E uses only 16 registers
  wire [3:0] rd_idx  = rd_raw[3:0];
  wire [3:0] rs1_idx = rs1_raw[3:0];
  wire [3:0] rs2_idx = rs2_raw[3:0];

  wire [31:0] rs1_val = (rs1_idx == 4'd0) ? 32'b0 : regs[rs1_idx];
  wire [31:0] rs2_val = (rs2_idx == 4'd0) ? 32'b0 : regs[rs2_idx];

  // Immediates
  wire [31:0] imm_i = {{20{instr[31]}}, instr[31:20]};
  wire [31:0] imm_s = {{20{instr[31]}}, instr[31:25], instr[11:7]};
  wire [31:0] imm_u = {instr[31:12], 12'b0};

  // -------------------------------
  // Control & ALU
  // -------------------------------
  // We only support a small subset, so use straightforward decoding.

  localparam OPCODE_OP      = 7'b0110011; // R-type (add)
  localparam OPCODE_OP_IMM  = 7'b0010011; // I-type (addi)
  localparam OPCODE_LUI     = 7'b0110111; // U-type (lui)
  localparam OPCODE_LOAD    = 7'b0000011; // I-type loads (lw, lbu)
  localparam OPCODE_STORE   = 7'b0100011; // S-type stores (sw, sb)
  localparam OPCODE_JALR    = 7'b1100111; // I-type jalr

  // funct3 encodings for the subset we care about
  localparam F3_ADD_SUB = 3'b000; // add
  localparam F3_LW      = 3'b010; // lw
  localparam F3_LBU     = 3'b100; // lbu
  localparam F3_SW      = 3'b010; // sw
  localparam F3_SB      = 3'b000; // sb

  // Register write-back signals
  reg        wb_en;
  reg [3:0]  wb_idx;
  reg [31:0] wb_data;

  wire [1:0] load_byte_off = dmem_addr[1:0];
  wire [7:0] load_byte = (load_byte_off == 2'b00) ? dmem_rdata[7:0] :
                         (load_byte_off == 2'b01) ? dmem_rdata[15:8] :
                         (load_byte_off == 2'b10) ? dmem_rdata[23:16] :
                                                   dmem_rdata[31:24];

  wire is_ebreak = (opcode == 7'b1110011) &&
                   (funct3 == 3'b000) &&
                   (rs1_raw == 5'd0) &&
                   (rd_raw == 5'd0) &&
                   (instr[31:20] == 12'h001);

  // Next PC
  reg [31:0] pc_next;

  always @(*) begin
    // defaults
    wb_en   = 1'b0;
    wb_idx  = rd_idx;
    wb_data = 32'b0;

    dmem_valid = 1'b0;
    dmem_we    = 1'b0;
    dmem_wmask = 4'b0000;
    dmem_addr  = 32'b0;
    dmem_wdata = 32'b0;

    trap = 1'b0;
    trap_code = 32'b0;

    pc_next = pc_next_seq;

    case (opcode)
      OPCODE_OP: begin
        // add: funct3=000, funct7=0000000
        if (!rd_raw[4] && !rs1_raw[4] && !rs2_raw[4] &&
            funct3 == F3_ADD_SUB && funct7 == 7'b0000000) begin
          wb_en   = (rd_idx != 4'd0);
          wb_data = rs1_val + rs2_val;
        end
      end

      OPCODE_OP_IMM: begin
        // addi: funct3=000
        if (!rd_raw[4] && !rs1_raw[4] && funct3 == F3_ADD_SUB) begin
          wb_en   = (rd_idx != 4'd0);
          wb_data = rs1_val + imm_i;
        end
      end

      OPCODE_LUI: begin
        // lui
        if (!rd_raw[4]) begin
          wb_en   = (rd_idx != 4'd0);
          wb_data = imm_u;
        end
      end

      OPCODE_LOAD: begin
        // lw, lbu
        if (!rd_raw[4] && !rs1_raw[4]) begin
          dmem_valid = 1'b1;
          dmem_we = 1'b0;
          dmem_addr = rs1_val + imm_i;
          case (funct3)
            F3_LW: begin
              // lw
              wb_en  = (rd_idx != 4'd0);
              wb_data = dmem_rdata;
            end
            F3_LBU: begin
              // lbu
              wb_en  = (rd_idx != 4'd0);
              wb_data = {24'b0, load_byte};
            end
            default: begin
            end
          endcase
        end
      end

      OPCODE_STORE: begin
        // sw, sb
        if (!rs1_raw[4] && !rs2_raw[4]) begin
          dmem_valid = 1'b1;
          dmem_we = 1'b1;
          dmem_addr = rs1_val + imm_s;
          case (funct3)
            F3_SW: begin
              dmem_wmask = 4'b1111;
              dmem_wdata = rs2_val;
            end
            F3_SB: begin
              case (dmem_addr[1:0])
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

      OPCODE_JALR: begin
        // jalr: rd, rs1, imm_i
        if (!rd_raw[4] && !rs1_raw[4] && funct3 == 3'b000) begin
          // link register gets next sequential PC
          wb_en   = (rd_idx != 4'd0);
          wb_data = pc_next_seq;
          // target: (rs1 + imm_i) & ~1
          pc_next = (rs1_val + imm_i) & ~32'b1;
        end
      end

      7'b1110011: begin
        if (is_ebreak) begin
          trap = 1'b1;
          trap_code = regs[10];
          npc_ebreak(pc, regs[10]);
          pc_next = pc;
        end
      end

      default: begin
        // unsupported instructions: treat as NOP (pc_next = pc + 4)
      end
    endcase
  end

  // -------------------------------
  // Sequential state updates
  // -------------------------------
  always @(posedge clk or posedge rst) begin
    if (rst) begin
      pc <= 32'h8000_0000;
      for (i = 0; i < 16; i = i + 1) begin
        regs[i] <= 32'b0;
      end
    end else begin
      // PC update
      pc <= pc_next;

      // Register write-back (x0 is hard-wired 0)
      if (wb_en && wb_idx != 4'd0) begin
        regs[wb_idx] <= wb_data;
      end
    end
  end

  // -------------------------------
  // Debug outputs
  // -------------------------------
  assign dbg_pc = pc;
  assign dbg_x1 = regs[1];

endmodule

/* verilator lint_on UNOPTFLAT */
