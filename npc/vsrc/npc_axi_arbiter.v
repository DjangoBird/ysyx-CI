module npc_axi_arbiter (
    input  wire        clk,
    input  wire        rst,

    input  wire        ifu_arvalid,
    output wire        ifu_arready,
    input  wire [31:0] ifu_araddr,
    output wire        ifu_rvalid,
    input  wire        ifu_rready,
    output wire [31:0] ifu_rdata,
    output wire [1:0]  ifu_rresp,

    input  wire        lsu_arvalid,
    output wire        lsu_arready,
    input  wire [31:0] lsu_araddr,
    output wire        lsu_rvalid,
    input  wire        lsu_rready,
    output wire [31:0] lsu_rdata,
    output wire [1:0]  lsu_rresp,
    input  wire        lsu_awvalid,
    output wire        lsu_awready,
    input  wire [31:0] lsu_awaddr,
    input  wire        lsu_wvalid,
    output wire        lsu_wready,
    input  wire [31:0] lsu_wdata,
    input  wire [3:0]  lsu_wstrb,
    output wire        lsu_bvalid,
    input  wire        lsu_bready,
    output wire [1:0]  lsu_bresp,

    output wire        out_arvalid,
    input  wire        out_arready,
    output wire [31:0] out_araddr,
    input  wire        out_rvalid,
    output wire        out_rready,
    input  wire [31:0] out_rdata,
    input  wire [1:0]  out_rresp,
    output wire        out_awvalid,
    input  wire        out_awready,
    output wire [31:0] out_awaddr,
    output wire        out_wvalid,
    input  wire        out_wready,
    output wire [31:0] out_wdata,
    output wire [3:0]  out_wstrb,
    input  wire        out_bvalid,
    output wire        out_bready,
    input  wire [1:0]  out_bresp
);
  localparam OWNER_NONE = 1'b0;
  localparam OWNER_LSU = 1'b1;

  reg read_busy;
  reg read_owner;
  wire choose_lsu = lsu_arvalid;
  wire selected_rready = read_owner == OWNER_LSU ? lsu_rready : ifu_rready;

  always @(posedge clk or posedge rst) begin
    if (rst) begin
      read_busy <= 1'b0;
      read_owner <= OWNER_NONE;
    end else begin
      if (!read_busy && out_arvalid && out_arready) begin
        read_busy <= 1'b1;
        read_owner <= choose_lsu ? OWNER_LSU : OWNER_NONE;
      end else if (read_busy && out_rvalid && out_rready) begin
        read_busy <= 1'b0;
      end
    end
  end

  assign out_arvalid = !read_busy && (lsu_arvalid || ifu_arvalid);
  assign out_araddr = choose_lsu ? lsu_araddr : ifu_araddr;
  assign lsu_arready = !read_busy && choose_lsu && out_arready;
  assign ifu_arready = !read_busy && !choose_lsu && ifu_arvalid && out_arready;

  assign out_rready = read_busy && selected_rready;
  assign lsu_rvalid = read_busy && read_owner == OWNER_LSU && out_rvalid;
  assign ifu_rvalid = read_busy && read_owner == OWNER_NONE && out_rvalid;
  assign lsu_rdata = out_rdata;
  assign ifu_rdata = out_rdata;
  assign lsu_rresp = out_rresp;
  assign ifu_rresp = out_rresp;

  assign out_awvalid = lsu_awvalid;
  assign lsu_awready = out_awready;
  assign out_awaddr = lsu_awaddr;
  assign out_wvalid = lsu_wvalid;
  assign lsu_wready = out_wready;
  assign out_wdata = lsu_wdata;
  assign out_wstrb = lsu_wstrb;
  assign lsu_bvalid = out_bvalid;
  assign out_bready = lsu_bready;
  assign lsu_bresp = out_bresp;
endmodule
