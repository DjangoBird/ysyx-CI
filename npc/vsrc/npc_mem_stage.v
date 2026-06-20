`include "minirv_defs.vh"

module npc_mem_stage(
    input  wire        in_valid,
    output wire        in_ready,
    output wire        out_valid,
    input  wire        out_ready,
    input  wire        wb_from_mem,
    input  wire [2:0]  load_funct3,
    input  wire [1:0]  load_byte_off,
    input  wire [31:0] dmem_rdata,
    input  wire [31:0] wb_data_pre,
    output reg  [31:0] wb_data
);
  assign in_ready = out_ready;
  assign out_valid = in_valid;

  always @(*) begin
    if (!wb_from_mem) begin
      wb_data = wb_data_pre;
    end else begin
      case (load_funct3)
        `F3_LB: begin
          case (load_byte_off)
            2'b00: wb_data = {{24{dmem_rdata[7]}}, dmem_rdata[7:0]};
            2'b01: wb_data = {{24{dmem_rdata[15]}}, dmem_rdata[15:8]};
            2'b10: wb_data = {{24{dmem_rdata[23]}}, dmem_rdata[23:16]};
            default: wb_data = {{24{dmem_rdata[31]}}, dmem_rdata[31:24]};
          endcase
        end
        `F3_LH: begin
          if (load_byte_off[1]) begin
            wb_data = {{16{dmem_rdata[31]}}, dmem_rdata[31:16]};
          end else begin
            wb_data = {{16{dmem_rdata[15]}}, dmem_rdata[15:0]};
          end
        end
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
        `F3_LHU: begin
          if (load_byte_off[1]) begin
            wb_data = {16'b0, dmem_rdata[31:16]};
          end else begin
            wb_data = {16'b0, dmem_rdata[15:0]};
          end
        end
        default: begin
          wb_data = wb_data_pre;
        end
      endcase
    end
  end
endmodule
