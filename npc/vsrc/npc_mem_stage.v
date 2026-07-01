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
    input  wire [31:0] axi_rdata,
    input  wire [1:0]  axi_rresp,
    input  wire [1:0]  axi_bresp,
    input  wire [31:0] wb_data_pre,
    input  wire        wb_en_in,
    input  wire [31:0] pc_next_in,
    input  wire [31:0] mtvec,
    output reg  [31:0] wb_data,
    output wire        wb_en_out,
    output wire [31:0] pc_next_out,
    output wire        access_fault,
    output wire [31:0] access_fault_cause,
    output wire        axi_arvalid,
    input  wire        axi_arready,
    output wire [31:0] axi_araddr,
    input  wire        axi_rvalid,
    output wire        axi_rready,
    output wire        axi_awvalid,
    input  wire        axi_awready,
    output wire [31:0] axi_awaddr,
    output wire        axi_wvalid,
    input  wire        axi_wready,
    output wire [31:0] axi_wdata,
    output wire [3:0]  axi_wstrb,
    input  wire        axi_bvalid,
    output wire        axi_bready,
    output wire        commit_mem_valid
);
  localparam [1:0] STATE_IDLE = 2'd0;
  localparam [1:0] STATE_READ_RESPONSE = 2'd1;
  localparam [1:0] STATE_WRITE_REQUEST = 2'd2;
  localparam [1:0] STATE_WRITE_RESPONSE = 2'd3;

  reg [1:0] state;
  reg aw_done;
  reg w_done;
  wire memory_operation = in_valid && dmem_valid_in;
  wire load_operation = memory_operation && !dmem_we_in;
  wire store_operation = memory_operation && dmem_we_in;
  wire aw_fire = axi_awvalid && axi_awready;
  wire w_fire = axi_wvalid && axi_wready;

  always @(posedge clk or posedge rst) begin
    if (rst) begin
      state <= STATE_IDLE;
      aw_done <= 1'b0;
      w_done <= 1'b0;
    end else begin
      case (state)
        STATE_IDLE: begin
          if (load_operation && axi_arvalid && axi_arready) begin
            state <= STATE_READ_RESPONSE;
          end else if (store_operation) begin
            state <= STATE_WRITE_REQUEST;
            aw_done <= 1'b0;
            w_done <= 1'b0;
          end
        end
        STATE_READ_RESPONSE: begin
          if (axi_rvalid && axi_rready) state <= STATE_IDLE;
        end
        STATE_WRITE_REQUEST: begin
          if (aw_fire) aw_done <= 1'b1;
          if (w_fire) w_done <= 1'b1;
          if ((aw_done || aw_fire) && (w_done || w_fire)) begin
            state <= STATE_WRITE_RESPONSE;
          end
        end
        STATE_WRITE_RESPONSE: begin
          if (axi_bvalid && axi_bready) state <= STATE_IDLE;
        end
        default: state <= STATE_IDLE;
      endcase
    end
  end

  assign in_ready = (state == STATE_IDLE) ? (!dmem_valid_in && out_ready)
      : (state == STATE_READ_RESPONSE) ? (axi_rvalid && out_ready)
      : (state == STATE_WRITE_RESPONSE) ? (axi_bvalid && out_ready)
      : 1'b0;
  assign out_valid = in_valid &&
      ((state == STATE_READ_RESPONSE && axi_rvalid) ||
       (state == STATE_WRITE_RESPONSE && axi_bvalid) ||
       (state == STATE_IDLE && !dmem_valid_in));

  assign axi_arvalid = load_operation && (state == STATE_IDLE);
  assign axi_araddr = dmem_addr_in;
  assign axi_rready = in_valid && (state == STATE_READ_RESPONSE) && out_ready;

  assign axi_awvalid = in_valid && (state == STATE_WRITE_REQUEST) && !aw_done;
  assign axi_awaddr = dmem_addr_in;
  assign axi_wvalid = in_valid && (state == STATE_WRITE_REQUEST) && !w_done;
  assign axi_wdata = dmem_wdata_in;
  assign axi_wstrb = dmem_wmask_in;
  assign axi_bready = in_valid && (state == STATE_WRITE_RESPONSE) && out_ready;

  assign commit_mem_valid = memory_operation &&
      ((state == STATE_READ_RESPONSE && axi_rvalid && out_ready) ||
       (state == STATE_WRITE_RESPONSE && axi_bvalid && out_ready));

  wire load_access_fault = state == STATE_READ_RESPONSE &&
                           axi_rvalid && axi_rresp != 2'b00;
  wire store_access_fault = state == STATE_WRITE_RESPONSE &&
                            axi_bvalid && axi_bresp != 2'b00;
  assign access_fault = in_valid &&
                        (load_access_fault || store_access_fault);
  assign access_fault_cause = load_access_fault ? 32'd5 : 32'd7;
  assign wb_en_out = access_fault ? 1'b0 : wb_en_in;
  assign pc_next_out = access_fault ? mtvec : pc_next_in;

  always @(*) begin
    if (!wb_from_mem) begin
      wb_data = wb_data_pre;
    end else begin
      case (load_funct3)
        `F3_LB: begin
          case (load_byte_off)
            2'b00: wb_data = {{24{axi_rdata[7]}}, axi_rdata[7:0]};
            2'b01: wb_data = {{24{axi_rdata[15]}}, axi_rdata[15:8]};
            2'b10: wb_data = {{24{axi_rdata[23]}}, axi_rdata[23:16]};
            default: wb_data = {{24{axi_rdata[31]}}, axi_rdata[31:24]};
          endcase
        end
        `F3_LH: wb_data = load_byte_off[1]
            ? {{16{axi_rdata[31]}}, axi_rdata[31:16]}
            : {{16{axi_rdata[15]}}, axi_rdata[15:0]};
        `F3_LW: wb_data = axi_rdata;
        `F3_LBU: begin
          case (load_byte_off)
            2'b00: wb_data = {24'b0, axi_rdata[7:0]};
            2'b01: wb_data = {24'b0, axi_rdata[15:8]};
            2'b10: wb_data = {24'b0, axi_rdata[23:16]};
            default: wb_data = {24'b0, axi_rdata[31:24]};
          endcase
        end
        `F3_LHU: wb_data = load_byte_off[1]
            ? {16'b0, axi_rdata[31:16]}
            : {16'b0, axi_rdata[15:0]};
        default: wb_data = wb_data_pre;
      endcase
    end
  end
endmodule
