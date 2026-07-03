module npc_if_stage(
    input  wire        clk,
    input  wire        rst,
    input  wire [31:0] pc,
    output wire        axi_arvalid,
    input  wire        axi_arready,
    output wire [31:0] axi_araddr,
    input  wire        axi_rvalid,
    output wire        axi_rready,
    input  wire [31:0] axi_rdata,
    input  wire [1:0]  axi_rresp,
    input  wire        out_ready,
    output wire        out_valid,
    output wire [31:0] instr,
    output wire        access_fault,
    output wire [31:0] pc_next_seq
);
  localparam [1:0] STATE_REQUEST = 2'd0;
  localparam [1:0] STATE_RESPONSE = 2'd1;
  localparam [1:0] STATE_OUTPUT = 2'd2;

  reg [1:0] state;
  reg [31:0] instr_reg;
  reg access_fault_reg;

  always @(posedge clk or posedge rst) begin
    if (rst) begin
      state <= STATE_REQUEST;
      instr_reg <= 32'b0;
      access_fault_reg <= 1'b0;
    end else begin
      case (state)
        STATE_REQUEST: begin
          if (axi_arvalid && axi_arready) state <= STATE_RESPONSE;
        end
        STATE_RESPONSE: begin
          if (axi_rvalid && axi_rready) begin
            instr_reg <= axi_rdata;
            access_fault_reg <= axi_rresp != 2'b00;
            state <= STATE_OUTPUT;
          end
        end
        STATE_OUTPUT: begin
          if (out_valid && out_ready) state <= STATE_REQUEST;
        end
        default: state <= STATE_REQUEST;
      endcase
    end
  end

  assign axi_arvalid = (state == STATE_REQUEST);
  assign axi_araddr = pc;
  assign axi_rready = (state == STATE_RESPONSE);
  assign out_valid = (state == STATE_OUTPUT);
  assign instr = instr_reg;
  assign access_fault = access_fault_reg;
  assign pc_next_seq = pc + 32'd4;
endmodule
