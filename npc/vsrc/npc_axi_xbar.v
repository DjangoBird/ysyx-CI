module npc_axi_xbar (
    input  wire        clk,
    input  wire        rst,

    input  wire        in_arvalid,
    output wire        in_arready,
    input  wire [31:0] in_araddr,
    output wire        in_rvalid,
    input  wire        in_rready,
    output wire [31:0] in_rdata,
    output wire [1:0]  in_rresp,
    input  wire        in_awvalid,
    output wire        in_awready,
    input  wire [31:0] in_awaddr,
    input  wire        in_wvalid,
    output wire        in_wready,
    input  wire [31:0] in_wdata,
    input  wire [3:0]  in_wstrb,
    output wire        in_bvalid,
    input  wire        in_bready,
    output wire [1:0]  in_bresp,

    output wire        mem_arvalid,
    input  wire        mem_arready,
    output wire [31:0] mem_araddr,
    input  wire        mem_rvalid,
    output wire        mem_rready,
    input  wire [31:0] mem_rdata,
    input  wire [1:0]  mem_rresp,
    output wire        mem_awvalid,
    input  wire        mem_awready,
    output wire [31:0] mem_awaddr,
    output wire        mem_wvalid,
    input  wire        mem_wready,
    output wire [31:0] mem_wdata,
    output wire [3:0]  mem_wstrb,
    input  wire        mem_bvalid,
    output wire        mem_bready,
    input  wire [1:0]  mem_bresp,

    output wire        uart_awvalid,
    input  wire        uart_awready,
    output wire [31:0] uart_awaddr,
    output wire        uart_wvalid,
    input  wire        uart_wready,
    output wire [31:0] uart_wdata,
    output wire [3:0]  uart_wstrb,
    input  wire        uart_bvalid,
    output wire        uart_bready,
    input  wire [1:0]  uart_bresp,

    output wire        clint_arvalid,
    input  wire        clint_arready,
    output wire [31:0] clint_araddr,
    input  wire        clint_rvalid,
    output wire        clint_rready,
    input  wire [31:0] clint_rdata,
    input  wire [1:0]  clint_rresp
);
  localparam [1:0] TARGET_MEM = 2'd0;
  localparam [1:0] TARGET_UART = 2'd1;
  localparam [1:0] TARGET_CLINT = 2'd2;
  localparam [1:0] TARGET_ERROR = 2'd3;

  localparam [1:0] WRITE_ADDR = 2'd0;
  localparam [1:0] WRITE_DATA = 2'd1;
  localparam [1:0] WRITE_RESP = 2'd2;

  reg read_busy;
  reg [1:0] read_target;
  reg error_rvalid;
  reg [1:0] write_state;
  reg [1:0] write_target;
  reg [31:0] write_addr;
  reg error_bvalid;

  function [1:0] decode_target;
    input [31:0] addr;
    begin
      if (addr >= 32'h8000_0000 && addr < 32'h8800_0000)
        decode_target = TARGET_MEM;
      else if (addr == 32'ha000_03f8)
        decode_target = TARGET_UART;
      else if (addr == 32'ha000_0048 || addr == 32'ha000_004c)
        decode_target = TARGET_CLINT;
      else
        decode_target = TARGET_ERROR;
    end
  endfunction

  wire [1:0] next_read_target = decode_target(in_araddr);
  wire selected_arready =
      next_read_target == TARGET_MEM ? mem_arready :
      next_read_target == TARGET_CLINT ? clint_arready : 1'b1;

  wire selected_rvalid =
      read_target == TARGET_MEM ? mem_rvalid :
      read_target == TARGET_CLINT ? clint_rvalid : error_rvalid;
  wire [31:0] selected_rdata =
      read_target == TARGET_MEM ? mem_rdata :
      read_target == TARGET_CLINT ? clint_rdata : 32'b0;
  wire [1:0] selected_rresp =
      read_target == TARGET_MEM ? mem_rresp :
      read_target == TARGET_CLINT ? clint_rresp : 2'b11;

  wire [1:0] next_write_target = decode_target(in_awaddr);
  wire selected_awready =
      next_write_target == TARGET_MEM ? mem_awready :
      next_write_target == TARGET_UART ? uart_awready : 1'b1;
  wire selected_wready =
      write_target == TARGET_MEM ? mem_wready :
      write_target == TARGET_UART ? uart_wready : 1'b1;
  wire selected_bvalid =
      write_target == TARGET_MEM ? mem_bvalid :
      write_target == TARGET_UART ? uart_bvalid : error_bvalid;
  wire [1:0] selected_bresp =
      write_target == TARGET_MEM ? mem_bresp :
      write_target == TARGET_UART ? uart_bresp : 2'b11;

  always @(posedge clk or posedge rst) begin
    if (rst) begin
      read_busy <= 1'b0;
      read_target <= TARGET_ERROR;
      error_rvalid <= 1'b0;
      write_state <= WRITE_ADDR;
      write_target <= TARGET_ERROR;
      write_addr <= 32'b0;
      error_bvalid <= 1'b0;
    end else begin
      if (!read_busy && in_arvalid && in_arready) begin
        read_busy <= 1'b1;
        read_target <= next_read_target;
        if (next_read_target == TARGET_UART ||
            next_read_target == TARGET_ERROR)
          error_rvalid <= 1'b1;
      end else if (read_busy && in_rvalid && in_rready) begin
        read_busy <= 1'b0;
        error_rvalid <= 1'b0;
      end

      case (write_state)
        WRITE_ADDR: begin
          if (in_awvalid && in_awready) begin
            write_target <= next_write_target;
            write_addr <= in_awaddr;
            write_state <= WRITE_DATA;
          end
        end
        WRITE_DATA: begin
          if (in_wvalid && in_wready) begin
            write_state <= WRITE_RESP;
            if (write_target == TARGET_CLINT ||
                write_target == TARGET_ERROR)
              error_bvalid <= 1'b1;
          end
        end
        WRITE_RESP: begin
          if (in_bvalid && in_bready) begin
            write_state <= WRITE_ADDR;
            error_bvalid <= 1'b0;
          end
        end
        default: write_state <= WRITE_ADDR;
      endcase
    end
  end

  assign in_arready = !read_busy && selected_arready;
  assign mem_arvalid = !read_busy && in_arvalid &&
                       next_read_target == TARGET_MEM;
  assign clint_arvalid = !read_busy && in_arvalid &&
                         next_read_target == TARGET_CLINT;
  assign mem_araddr = in_araddr;
  assign clint_araddr = in_araddr;

  assign in_rvalid = read_busy && selected_rvalid;
  assign in_rdata = selected_rdata;
  assign in_rresp = selected_rresp;
  assign mem_rready = read_busy && read_target == TARGET_MEM && in_rready;
  assign clint_rready = read_busy && read_target == TARGET_CLINT && in_rready;

  assign in_awready = write_state == WRITE_ADDR && selected_awready;
  assign mem_awvalid = write_state == WRITE_ADDR && in_awvalid &&
                       next_write_target == TARGET_MEM;
  assign uart_awvalid = write_state == WRITE_ADDR && in_awvalid &&
                        next_write_target == TARGET_UART;
  assign mem_awaddr = in_awaddr;
  assign uart_awaddr = in_awaddr;

  assign in_wready = write_state == WRITE_DATA && selected_wready;
  assign mem_wvalid = write_state == WRITE_DATA && in_wvalid &&
                      write_target == TARGET_MEM;
  assign uart_wvalid = write_state == WRITE_DATA && in_wvalid &&
                       write_target == TARGET_UART;
  assign mem_wdata = in_wdata;
  assign uart_wdata = in_wdata;
  assign mem_wstrb = in_wstrb;
  assign uart_wstrb = in_wstrb;

  assign in_bvalid = write_state == WRITE_RESP && selected_bvalid;
  assign in_bresp = selected_bresp;
  assign mem_bready = write_state == WRITE_RESP &&
                      write_target == TARGET_MEM && in_bready;
  assign uart_bready = write_state == WRITE_RESP &&
                       write_target == TARGET_UART && in_bready;

  wire unused_write_addr = |write_addr;
endmodule
