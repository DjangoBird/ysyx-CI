module npc_axi_uart (
    input  wire        clk,
    input  wire        rst,
    input  wire        awvalid,
    output wire        awready,
    input  wire [31:0] awaddr,
    input  wire        wvalid,
    output wire        wready,
    input  wire [31:0] wdata,
    input  wire [3:0]  wstrb,
    output wire        bvalid,
    input  wire        bready,
    output wire [1:0]  bresp
);
  reg address_done;
  reg response_valid;

  always @(posedge clk or posedge rst) begin
    if (rst) begin
      address_done <= 1'b0;
      response_valid <= 1'b0;
    end else begin
      if (awvalid && awready)
        address_done <= 1'b1;

      if (wvalid && wready) begin
        if (wstrb[0]) begin
`ifdef VERILATOR
          $write("%c", wdata[7:0]);
          $fflush();
`endif
        end
        response_valid <= 1'b1;
      end

      if (bvalid && bready) begin
        address_done <= 1'b0;
        response_valid <= 1'b0;
      end
    end
  end

  assign awready = !address_done && !response_valid;
  assign wready = address_done && !response_valid;
  assign bvalid = response_valid;
  assign bresp = 2'b00;

  wire unused_awaddr = |awaddr;
  wire unused_wstrb = |wstrb[3:1];
  wire unused_wdata = |wdata[31:8];
endmodule
