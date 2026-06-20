module npc_csr_file(
    input  wire        clk,
    input  wire        rst,
    input  wire [11:0] read_addr,
    output reg  [31:0] read_data,
    input  wire        write_enable,
    input  wire [11:0] write_addr,
    input  wire [31:0] write_data,
    input  wire        ecall,
    input  wire [31:0] ecall_pc,
    output wire [31:0] mtvec,
    output wire [31:0] mepc
);
  reg [63:0] mcycle;
  reg [31:0] mstatus;
  reg [31:0] mtvec_reg;
  reg [31:0] mepc_reg;
  reg [31:0] mcause;

  always @(posedge clk or posedge rst) begin
    if (rst) begin
      mcycle <= 64'b0;
      mstatus <= 32'h0000_1800;
      mtvec_reg <= 32'b0;
      mepc_reg <= 32'b0;
      mcause <= 32'b0;
    end else begin
      mcycle <= mcycle + 64'd1;
      if (write_enable) begin
        case (write_addr)
          12'h300: mstatus <= write_data;
          12'h305: mtvec_reg <= write_data;
          12'h341: mepc_reg <= write_data;
          12'h342: mcause <= write_data;
          default: begin end
        endcase
      end
      if (ecall) begin
        mepc_reg <= ecall_pc;
        mcause <= 32'd11;
      end
    end
  end

  always @(*) begin
    case (read_addr)
      12'hb00: read_data = mcycle[31:0];
      12'hb80: read_data = mcycle[63:32];
      12'hf11: read_data = 32'h7973_7978;
      12'hf12: read_data = 32'd22040000;
      12'h300: read_data = mstatus;
      12'h305: read_data = mtvec_reg;
      12'h341: read_data = mepc_reg;
      12'h342: read_data = mcause;
      default: read_data = 32'b0;
    endcase
  end

  assign mtvec = mtvec_reg;
  assign mepc = mepc_reg;
endmodule
