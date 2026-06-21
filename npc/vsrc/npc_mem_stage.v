`include "minirv_defs.vh"

module npc_mem_stage(
    input  wire        clk,
    input  wire        rst,
    input  wire        in_valid,
    output wire        in_ready,
    output wire        out_valid,
    input  wire        out_ready,
    input  wire        dmem_valid_in,
    input  wire        dmem_we_in,
    input  wire [3:0]  dmem_wmask_in,
    input  wire [31:0] dmem_addr_in,
    input  wire [31:0] dmem_wdata_in,
    input  wire        wb_from_mem,
    input  wire [2:0]  load_funct3,
    input  wire [1:0]  load_byte_off,
    input  wire [31:0] dmem_rdata,
    input  wire [31:0] wb_data_pre,
    output reg  [31:0] wb_data,
    output wire        dmem_req_valid,
    input  wire        dmem_req_ready,
    output wire        dmem_req_we,
    output wire [3:0]  dmem_req_wmask,
    output wire [31:0] dmem_req_addr,
    output wire [31:0] dmem_req_wdata,
    input  wire        dmem_resp_valid,
    output wire        dmem_resp_ready,
    output wire        commit_mem_valid
);
  localparam STATE_IDLE = 1'b0;
  localparam STATE_WAIT = 1'b1;

  reg state;
  wire memory_operation = in_valid && dmem_valid_in;

  always @(posedge clk or posedge rst) begin
    if (rst) begin
      state <= STATE_IDLE;
    end else begin
      case (state)
        STATE_IDLE: begin
          if (dmem_req_valid && dmem_req_ready) state <= STATE_WAIT;
        end
        STATE_WAIT: begin
          if (dmem_resp_valid && dmem_resp_ready) state <= STATE_IDLE;
        end
        default: state <= STATE_IDLE;
      endcase
    end
  end

  assign in_ready = (state == STATE_IDLE) ? (!dmem_valid_in && out_ready)
                                         : (dmem_resp_valid && out_ready);
  assign out_valid = in_valid &&
                     ((state == STATE_WAIT && dmem_resp_valid) ||
                      (state == STATE_IDLE && !dmem_valid_in));
  assign dmem_req_valid = memory_operation && (state == STATE_IDLE);
  assign dmem_req_we = dmem_we_in;
  assign dmem_req_wmask = dmem_wmask_in;
  assign dmem_req_addr = dmem_addr_in;
  assign dmem_req_wdata = dmem_wdata_in;
  assign dmem_resp_ready = in_valid && (state == STATE_WAIT) && out_ready;
  assign commit_mem_valid = memory_operation && (state == STATE_WAIT) &&
                            dmem_resp_valid && out_ready;

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
