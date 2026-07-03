module npc_axi_clint (
    input  wire        clk,
    input  wire        rst,
    input  wire        arvalid,
    output wire        arready,
    input  wire [31:0] araddr,
    output wire        rvalid,
    input  wire        rready,
    output wire [31:0] rdata,
    output wire [1:0]  rresp
);
  reg [63:0] mtime;
  reg response_valid;
  reg [31:0] response_data;

  always @(posedge clk or posedge rst) begin
    if (rst) begin
      mtime <= 64'b0;
      response_valid <= 1'b0;
      response_data <= 32'b0;
    end else begin
      mtime <= mtime + 64'd1;
      if (arvalid && arready) begin
        response_valid <= 1'b1;
        response_data <= araddr[2] ? mtime[63:32] : mtime[31:0];
      end
      if (rvalid && rready)
        response_valid <= 1'b0;
    end
  end

  assign arready = !response_valid;
  assign rvalid = response_valid;
  assign rdata = response_data;
  assign rresp = 2'b00;

  wire unused_araddr = |{araddr[31:3], araddr[1:0]};
endmodule
