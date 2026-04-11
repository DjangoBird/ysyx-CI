/* verilator lint_off UNUSEDSIGNAL */
/* verilator lint_off PINCONNECTEMPTY */

module top(
    input        clk,    // 时钟
    input        rst,    // 复位，高电平有效
    output [7:0] led     // 8 个 LED
);

  // 实例化最小 RV32E minirv 处理器
  wire [31:0] dbg_pc;
  wire [31:0] dbg_x1;
  wire [31:0] dbg_x2;

  minirv_core u_core (
    .clk    (clk),
    .rst    (rst),
    .dbg_pc (dbg_pc),
    .dbg_x1 (dbg_x1),
    .dbg_x2 (dbg_x2)
  );

  // 将 PC 的低 8 位接到 LED 上
  assign led = dbg_pc[7:0];

endmodule

/* verilator lint_on UNUSEDSIGNAL */
/* verilator lint_on PINCONNECTEMPTY */
