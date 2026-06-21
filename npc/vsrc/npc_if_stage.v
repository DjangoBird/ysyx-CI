module npc_if_stage(
    input  wire        clk,
    input  wire        rst,
    input  wire [31:0] pc,
    input  wire [31:0] imem_rdata,
    output wire        imem_req_valid,
    input  wire        imem_req_ready,
    input  wire        imem_resp_valid,
    output wire        imem_resp_ready,
    input  wire        out_ready,
    output wire        out_valid,
    output wire [31:0] imem_addr,
    output wire [31:0] instr,
    output wire [31:0] pc_next_seq
);
  localparam STATE_REQUEST = 1'b0;
  localparam STATE_RESPONSE = 1'b1;

  reg state;

  always @(posedge clk or posedge rst) begin
    if (rst) begin
      state <= STATE_REQUEST;
    end else begin
      case (state)
        STATE_REQUEST: begin
          if (imem_req_valid && imem_req_ready) state <= STATE_RESPONSE;
        end
        STATE_RESPONSE: begin
          if (imem_resp_valid && imem_resp_ready) state <= STATE_REQUEST;
        end
        default: state <= STATE_REQUEST;
      endcase
    end
  end

  assign imem_req_valid = (state == STATE_REQUEST);
  assign imem_resp_ready = (state == STATE_RESPONSE) && out_ready;
  assign out_valid = (state == STATE_RESPONSE) && imem_resp_valid;
  assign imem_addr = pc;
  assign instr = imem_rdata;
  assign pc_next_seq = pc + 32'd4;
endmodule
