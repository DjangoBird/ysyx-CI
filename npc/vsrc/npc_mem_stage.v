`include "minirv_defs.vh"

module npc_mem_stage(
    input  wire        wb_from_mem,
    input  wire [2:0]  load_funct3,
    input  wire [1:0]  load_byte_off,
    input  wire [31:0] dmem_rdata,
    input  wire [31:0] wb_data_pre,
    output reg  [31:0] wb_data
);
  always @(*) begin
    if (!wb_from_mem) begin
      wb_data = wb_data_pre;
    end else begin
      case (load_funct3)
        `F3_LW: begin
          wb_data = dmem_rdata;
        end
        `F3_LBU: begin
          case (load_byte_off)
            2'b00: wb_data = {24'b0, dmem_rdata[7:0]};
            2'b01: wb_data = {24'b0, dmem_rdata[15:8]};
            2'b10: wb_data = {24'b0, dmem_rdata[23:16]};
            default: wb_data = {24'b0, dmem_rdata[31:24]};
          endcase
        end
        default: begin
          wb_data = wb_data_pre;
        end
      endcase
    end
  end
endmodule
