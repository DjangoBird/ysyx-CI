`include "minirv_defs.vh"

module minirv_core (
    input  wire         clk,
    input  wire         rst,
    input  wire         commit_ready,

  output wire         ifu_axi_arvalid,
  input  wire         ifu_axi_arready,
  output wire [31:0]  ifu_axi_araddr,
  input  wire         ifu_axi_rvalid,
  output wire         ifu_axi_rready,
  input  wire [31:0]  ifu_axi_rdata,
  input  wire [1:0]   ifu_axi_rresp,

  output wire         lsu_axi_arvalid,
  input  wire         lsu_axi_arready,
  output wire [31:0]  lsu_axi_araddr,
  input  wire         lsu_axi_rvalid,
  output wire         lsu_axi_rready,
  input  wire [31:0]  lsu_axi_rdata,
  input  wire [1:0]   lsu_axi_rresp,
  output wire         lsu_axi_awvalid,
  input  wire         lsu_axi_awready,
  output wire [31:0]  lsu_axi_awaddr,
  output wire         lsu_axi_wvalid,
  input  wire         lsu_axi_wready,
  output wire [31:0]  lsu_axi_wdata,
  output wire [3:0]   lsu_axi_wstrb,
  input  wire         lsu_axi_bvalid,
  output wire         lsu_axi_bready,
  input  wire [1:0]   lsu_axi_bresp,

  // trap interface
  output wire         trap,
  output wire [31:0]  trap_code,
  output wire         commit_valid,
  output wire [31:0]  commit_pc,
  output wire [31:0]  commit_instr,
  output wire [31:0]  commit_next_pc,
  output wire         commit_mem_valid,
  output wire         commit_mem_we,
  output wire [3:0]   commit_mem_wmask,
  output wire [31:0]  commit_mem_addr,
  output wire [31:0]  commit_mem_wdata,
  output wire [31:0]  commit_mem_rdata,

  // debug observation port
  output wire [31:0]  dbg_pc,
  output wire [31:0]  dbg_x0,
  output wire [31:0]  dbg_x1,
  output wire [31:0]  dbg_x2,
  output wire [31:0]  dbg_x3,
  output wire [31:0]  dbg_x4,
  output wire [31:0]  dbg_x5,
  output wire [31:0]  dbg_x6,
  output wire [31:0]  dbg_x7,
  output wire [31:0]  dbg_x8,
  output wire [31:0]  dbg_x9,
  output wire [31:0]  dbg_x10,
  output wire [31:0]  dbg_x11,
  output wire [31:0]  dbg_x12,
  output wire [31:0]  dbg_x13,
  output wire [31:0]  dbg_x14,
  output wire [31:0]  dbg_x15
);

  wire [31:0] pc;
  wire [31:0] instr;
  wire [31:0] pc_next_seq;
  wire        instruction_access_fault;

  wire [6:0]  opcode;
  wire [4:0]  rd_raw;
  wire [2:0]  funct3;
  wire [4:0]  rs1_raw;
  wire [4:0]  rs2_raw;
  wire [6:0]  funct7;
  wire [3:0]  rd_idx;
  wire [3:0]  rs1_idx;
  wire [3:0]  rs2_idx;
  wire [31:0] imm_i;
  wire [31:0] imm_s;
  wire [31:0] imm_b;
  wire [31:0] imm_u;
  wire [31:0] imm_j;
  wire        is_ebreak;

  wire [31:0] rs1_val;
  wire [31:0] rs2_val;
  wire [31:0] a0_val;

  wire        ex_wb_en;
  wire        wb_en;
  wire [3:0]  wb_idx;
  wire [31:0] wb_data_pre;
  wire        wb_from_mem;
  wire [1:0]  load_byte_off;
  wire [2:0]  load_funct3;
  wire [31:0] wb_data;
  wire [31:0] ex_pc_next;
  wire [31:0] pc_next;
  wire [31:0] csr_read_data;
  wire [31:0] mtvec;
  wire [31:0] mepc;
  wire [11:0] csr_addr;
  wire        csr_write_enable;
  wire [31:0] csr_write_data;
  wire        ecall;
  wire        ex_access_fault;
  wire [31:0] ex_access_fault_cause;
  wire        mem_access_fault;
  wire [31:0] mem_access_fault_cause;
  wire        if_valid;
  wire        if_ready;
  wire        id_valid;
  wire        id_ready;
  wire        ex_valid;
  wire        ex_ready;
  wire        mem_valid;
  wire        mem_ready;
  wire        ex_dmem_valid;
  wire        ex_dmem_we;
  wire [3:0]  ex_dmem_wmask;
  wire [31:0] ex_dmem_addr;
  wire [31:0] ex_dmem_wdata;

  npc_wb_stage u_wb (
    .clk     (clk),
    .rst     (rst),
    .commit_ready(commit_ready),
    .in_valid(mem_valid),
    .in_ready(mem_ready),
    .pc_next (pc_next),
    .wb_en   (wb_en),
    .wb_idx  (wb_idx),
    .wb_data (wb_data),
    .pc      (pc),
    .dbg_x0  (dbg_x0),
    .dbg_x1  (dbg_x1),
    .dbg_x2  (dbg_x2),
    .dbg_x3  (dbg_x3),
    .dbg_x4  (dbg_x4),
    .dbg_x5  (dbg_x5),
    .dbg_x6  (dbg_x6),
    .dbg_x7  (dbg_x7),
    .dbg_x8  (dbg_x8),
    .dbg_x9  (dbg_x9),
    .dbg_x10 (dbg_x10),
    .dbg_x11 (dbg_x11),
    .dbg_x12 (dbg_x12),
    .dbg_x13 (dbg_x13),
    .dbg_x14 (dbg_x14),
    .dbg_x15 (dbg_x15)
  );

  npc_if_stage u_if (
    .clk        (clk),
    .rst        (rst),
    .pc         (pc),
    .axi_arvalid(ifu_axi_arvalid),
    .axi_arready(ifu_axi_arready),
    .axi_araddr (ifu_axi_araddr),
    .axi_rvalid (ifu_axi_rvalid),
    .axi_rready (ifu_axi_rready),
    .axi_rdata  (ifu_axi_rdata),
    .axi_rresp  (ifu_axi_rresp),
    .out_ready  (if_ready),
    .out_valid  (if_valid),
    .instr      (instr),
    .access_fault(instruction_access_fault),
    .pc_next_seq(pc_next_seq)
  );

  npc_id_stage u_id (
    .in_valid (if_valid),
    .in_ready (if_ready),
    .out_valid(id_valid),
    .out_ready(id_ready),
    .instr    (instr),
    .opcode   (opcode),
    .rd_raw   (rd_raw),
    .funct3   (funct3),
    .rs1_raw  (rs1_raw),
    .rs2_raw  (rs2_raw),
    .funct7   (funct7),
    .rd_idx   (rd_idx),
    .rs1_idx  (rs1_idx),
    .rs2_idx  (rs2_idx),
    .imm_i    (imm_i),
    .imm_s    (imm_s),
    .imm_b    (imm_b),
    .imm_u    (imm_u),
    .imm_j    (imm_j),
    .is_ebreak(is_ebreak)
  );

  function [31:0] read_reg;
    input [3:0] idx;
    begin
      case (idx)
        4'd0: read_reg = dbg_x0;
        4'd1: read_reg = dbg_x1;
        4'd2: read_reg = dbg_x2;
        4'd3: read_reg = dbg_x3;
        4'd4: read_reg = dbg_x4;
        4'd5: read_reg = dbg_x5;
        4'd6: read_reg = dbg_x6;
        4'd7: read_reg = dbg_x7;
        4'd8: read_reg = dbg_x8;
        4'd9: read_reg = dbg_x9;
        4'd10: read_reg = dbg_x10;
        4'd11: read_reg = dbg_x11;
        4'd12: read_reg = dbg_x12;
        4'd13: read_reg = dbg_x13;
        4'd14: read_reg = dbg_x14;
        4'd15: read_reg = dbg_x15;
        default: read_reg = 32'b0;
      endcase
    end
  endfunction

  assign rs1_val = read_reg(rs1_idx);
  assign rs2_val = read_reg(rs2_idx);
  assign a0_val = dbg_x10;

  npc_csr_file u_csr (
    .clk          (clk),
    .rst          (rst),
    .read_addr    (imm_i[11:0]),
    .read_data    (csr_read_data),
    .write_enable (ex_valid && ex_ready && csr_write_enable),
    .write_addr   (csr_addr),
    .write_data   (csr_write_data),
    .ecall        (ex_valid && ex_ready && ecall),
    .ecall_pc     (pc),
    .access_fault ((ex_valid && ex_ready && ex_access_fault) ||
                   (mem_valid && mem_ready && mem_access_fault)),
    .access_fault_cause(mem_access_fault
        ? mem_access_fault_cause : ex_access_fault_cause),
    .access_fault_pc(pc),
    .mtvec        (mtvec),
    .mepc         (mepc)
  );

  npc_ex_stage u_ex (
    .in_valid     (id_valid),
    .in_ready     (id_ready),
    .out_valid    (ex_valid),
    .out_ready    (ex_ready),
    .opcode        (opcode),
    .rd_raw        (rd_raw),
    .funct3        (funct3),
    .rs1_raw       (rs1_raw),
    .rs2_raw       (rs2_raw),
    .funct7        (funct7),
    .rd_idx        (rd_idx),
    .rs1_val       (rs1_val),
    .rs2_val       (rs2_val),
    .imm_i         (imm_i),
    .imm_s         (imm_s),
    .imm_b         (imm_b),
    .imm_u         (imm_u),
    .imm_j         (imm_j),
    .a0_val        (a0_val),
    .pc            (pc),
    .pc_next_seq   (pc_next_seq),
    .instruction_access_fault(instruction_access_fault),
    .is_ebreak     (is_ebreak),
    .csr_read_data (csr_read_data),
    .mtvec         (mtvec),
    .mepc          (mepc),
    .wb_en         (ex_wb_en),
    .wb_idx        (wb_idx),
    .wb_data_pre   (wb_data_pre),
    .wb_from_mem   (wb_from_mem),
    .dmem_valid    (ex_dmem_valid),
    .dmem_we       (ex_dmem_we),
    .dmem_wmask    (ex_dmem_wmask),
    .dmem_addr     (ex_dmem_addr),
    .dmem_wdata    (ex_dmem_wdata),
    .load_byte_off (load_byte_off),
    .load_funct3   (load_funct3),
    .pc_next       (ex_pc_next),
    .trap          (trap),
    .trap_code     (trap_code),
    .csr_addr      (csr_addr),
    .csr_write_enable(csr_write_enable),
    .csr_write_data(csr_write_data),
    .ecall         (ecall),
    .access_fault  (ex_access_fault),
    .access_fault_cause(ex_access_fault_cause)
  );

  npc_mem_stage u_mem (
    .clk           (clk),
    .rst           (rst),
    .in_valid      (ex_valid),
    .in_ready      (ex_ready),
    .out_valid     (mem_valid),
    .out_ready     (mem_ready),
    .dmem_valid_in (ex_dmem_valid),
    .dmem_we_in    (ex_dmem_we),
    .dmem_wmask_in (ex_dmem_wmask),
    .dmem_addr_in  (ex_dmem_addr),
    .dmem_wdata_in (ex_dmem_wdata),
    .wb_from_mem   (wb_from_mem),
    .load_funct3   (load_funct3),
    .load_byte_off (load_byte_off),
    .axi_rdata     (lsu_axi_rdata),
    .axi_rresp     (lsu_axi_rresp),
    .axi_bresp     (lsu_axi_bresp),
    .wb_data_pre   (wb_data_pre),
    .wb_en_in      (ex_wb_en),
    .pc_next_in    (ex_pc_next),
    .mtvec         (mtvec),
    .wb_data       (wb_data),
    .wb_en_out     (wb_en),
    .pc_next_out   (pc_next),
    .access_fault  (mem_access_fault),
    .access_fault_cause(mem_access_fault_cause),
    .axi_arvalid   (lsu_axi_arvalid),
    .axi_arready   (lsu_axi_arready),
    .axi_araddr    (lsu_axi_araddr),
    .axi_rvalid    (lsu_axi_rvalid),
    .axi_rready    (lsu_axi_rready),
    .axi_awvalid   (lsu_axi_awvalid),
    .axi_awready   (lsu_axi_awready),
    .axi_awaddr    (lsu_axi_awaddr),
    .axi_wvalid    (lsu_axi_wvalid),
    .axi_wready    (lsu_axi_wready),
    .axi_wdata     (lsu_axi_wdata),
    .axi_wstrb     (lsu_axi_wstrb),
    .axi_bvalid    (lsu_axi_bvalid),
    .axi_bready    (lsu_axi_bready),
    .commit_mem_valid(commit_mem_valid)
  );

  assign dbg_pc = pc;
  assign commit_valid = mem_valid && mem_ready;
  assign commit_pc = pc;
  assign commit_instr = instr;
  assign commit_next_pc = pc_next;
  assign commit_mem_we = ex_dmem_we;
  assign commit_mem_wmask = ex_dmem_wmask;
  assign commit_mem_addr = ex_dmem_addr;
  assign commit_mem_wdata = ex_dmem_wdata;
  assign commit_mem_rdata = lsu_axi_rdata;

endmodule
