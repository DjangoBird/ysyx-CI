// Minimal single-cycle RV32E (minirv) core
// PC initial value = 0
// 16 GPRs (x0-x15), x0 hard-wired to 0
// Supported instructions: add, addi, lui, lw, lbu, sw, sb, jalr
// Other ISA details follow RV32I

/* verilator lint_off WIDTHTRUNC */
/* verilator lint_off UNUSEDSIGNAL */
/* verilator lint_off CASEINCOMPLETE */

module minirv_core (
    input  wire         clk,
    input  wire         rst,

    // simple debug observation ports (optional)
    output wire [31:0]  dbg_pc,
    output wire [31:0]  dbg_x1,
    output wire [31:0]  dbg_x2
);

  // -------------------------------
  // PC & Register File (RV32E: 16 regs)
  // -------------------------------
  reg [31:0] pc;
  reg [31:0] regs[0:15];

  integer i;

  // -------------------------------
  // Simple instruction / data memories (internal)
  // -------------------------------
  // For simplicity, we model separate I/D memories.
  // In practice you may want to expose a bus interface instead.

  localparam IMEM_WORDS = 1024;   // 4KB instruction memory
  localparam DMEM_BYTES = 4096;   // 4KB data memory

  reg [31:0] imem[0:IMEM_WORDS-1];
  reg [7:0]  dmem[0:DMEM_BYTES-1];

  // optional: initialize instruction/data memory
  integer j;
  initial begin
    // clear memories to 0 so that default instruction is NOP (addi x0,x0,0)
    for (j = 0; j < IMEM_WORDS; j = j + 1) begin
      imem[j] = 32'b0;
    end
    for (j = 0; j < DMEM_BYTES; j = j + 1) begin
      dmem[j] = 8'b0;
    end
    // 如果需要加载程序，可取消下面一行注释，并提供 prog.hex
    // $readmemh("prog.hex", imem);
  end

  // -------------------------------
  // Instruction Fetch
  // -------------------------------
  wire [31:0] instr;
  wire [31:0] pc_next_seq = pc + 32'd4;

  // word-aligned fetch (ignore最低2位，使用 10bit 下标访问 1024word 的 imem)
  assign instr = imem[pc[11:2]];

  // -------------------------------
  // Decode fields (RV32I encoding)
  // -------------------------------
  wire [6:0] opcode = instr[6:0];
  wire [4:0] rd_raw  = instr[11:7];
  wire [2:0] funct3 = instr[14:12];
  wire [4:0] rs1_raw = instr[19:15];
  wire [4:0] rs2_raw = instr[24:20];
  wire [6:0] funct7 = instr[31:25];

  // RV32E uses only 16 registers: index low 4 bits
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

  // Memory access signals
  reg        mem_we;
  reg        mem_re;
  reg [31:0] mem_addr;
  reg [31:0] mem_wdata;
  wire [31:0] mem_rdata_word;
  wire [7:0]  mem_rdata_byte;

  // Combinational read from data memory (little-endian)
  assign mem_rdata_word = { dmem[mem_addr + 32'd3],
                            dmem[mem_addr + 32'd2],
                            dmem[mem_addr + 32'd1],
                            dmem[mem_addr + 32'd0] };

  assign mem_rdata_byte = dmem[mem_addr];

  // Next PC
  reg [31:0] pc_next;

  always @(*) begin
    // defaults
    wb_en   = 1'b0;
    wb_idx  = rd_idx;
    wb_data = 32'b0;

    mem_we   = 1'b0;
    mem_re   = 1'b0;
    mem_addr = 32'b0;
    mem_wdata = 32'b0;

    pc_next = pc_next_seq;

    case (opcode)
      OPCODE_OP: begin
        // add: funct3=000, funct7=0000000
        if (funct3 == F3_ADD_SUB && funct7 == 7'b0000000) begin
          wb_en   = (rd_idx != 4'd0);
          wb_data = rs1_val + rs2_val;
        end
      end

      OPCODE_OP_IMM: begin
        // addi: funct3=000
        if (funct3 == F3_ADD_SUB) begin
          wb_en   = (rd_idx != 4'd0);
          wb_data = rs1_val + imm_i;
        end
      end

      OPCODE_LUI: begin
        // lui
        wb_en   = (rd_idx != 4'd0);
        wb_data = imm_u;
      end

      OPCODE_LOAD: begin
        // lw, lbu
        mem_addr = rs1_val + imm_i;
        case (funct3)
          F3_LW: begin
            // lw
            mem_re = 1'b1;
            wb_en  = (rd_idx != 4'd0);
            wb_data = mem_rdata_word;
          end
          F3_LBU: begin
            // lbu
            mem_re = 1'b1;
            wb_en  = (rd_idx != 4'd0);
            wb_data = {24'b0, mem_rdata_byte};
          end
        endcase
      end

      OPCODE_STORE: begin
        // sw, sb
        mem_addr = rs1_val + imm_s;
        case (funct3)
          F3_SW: begin
            mem_we   = 1'b1;
            mem_wdata = rs2_val;
          end
          F3_SB: begin
            mem_we   = 1'b1;
            mem_wdata = {24'b0, rs2_val[7:0]};
          end
        endcase
      end

      OPCODE_JALR: begin
        // jalr: rd, rs1, imm_i
        if (funct3 == 3'b000) begin
          // link register gets next sequential PC
          wb_en   = (rd_idx != 4'd0);
          wb_data = pc_next_seq;
          // target: (rs1 + imm_i) & ~1
          pc_next = (rs1_val + imm_i) & ~32'b1;
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
      pc <= 32'b0;
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

      // Data memory write (little-endian)
      if (mem_we) begin
        // sw / sb based on funct3 already encoded in mem_wdata
        // For sw, mem_wdata holds full word; for sb, only low byte is used
        if (funct3 == F3_SW) begin
          // write 4 bytes
          dmem[mem_addr + 32'd0] <= mem_wdata[7:0];
          dmem[mem_addr + 32'd1] <= mem_wdata[15:8];
          dmem[mem_addr + 32'd2] <= mem_wdata[23:16];
          dmem[mem_addr + 32'd3] <= mem_wdata[31:24];
        end else if (funct3 == F3_SB) begin
          dmem[mem_addr] <= mem_wdata[7:0];
        end
      end
    end
  end

  // -------------------------------
  // Debug outputs
  // -------------------------------
  assign dbg_pc = pc;
  assign dbg_x1 = regs[1];
  assign dbg_x2 = regs[2];

endmodule

/* verilator lint_on WIDTHTRUNC */
/* verilator lint_on UNUSEDSIGNAL */
/* verilator lint_on CASEINCOMPLETE */
