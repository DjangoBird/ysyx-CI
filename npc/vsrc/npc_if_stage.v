module npc_if_stage(
    input  wire        clk,
    input  wire        rst,
    input  wire [31:0] pc,
    input  wire [31:0] imem_rdata,
    input  wire        out_ready,
    output wire        out_valid,
    output wire [31:0] imem_addr,
    output wire [31:0] instr,
    output wire [31:0] pc_next_seq
);
  localparam STATE_IDLE = 1'b0;
  localparam STATE_WAIT = 1'b1;

  reg state;

  always @(posedge clk or posedge rst) begin
    if (rst) begin
      state <= STATE_IDLE;
    end else begin
      case (state)
        STATE_IDLE: state <= STATE_WAIT;
        STATE_WAIT: begin
          if (out_ready) state <= STATE_IDLE;
        end
        default: state <= STATE_IDLE;
      endcase
    end
  end

  assign out_valid = (state == STATE_WAIT);
  assign imem_addr = pc;
  assign instr = imem_rdata;
  assign pc_next_seq = pc + 32'd4;
endmodule
