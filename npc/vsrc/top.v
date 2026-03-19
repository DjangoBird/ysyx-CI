module top(
    input        clk,    // 时钟
    input        rst,    // 复位，高电平有效
    output [7:0] led     // 8 个 LED
);

  reg [23:0] cnt;        // 分频计数器，控制流水灯速度
  reg [7:0]  pattern;    // 当前点亮的灯

  always @(posedge clk or posedge rst) begin
    if (rst) begin
      cnt     <= 24'd0;
      pattern <= 8'b0000_0001;   // 上电后先亮 LD0
    end else begin
      cnt <= cnt + 1;
      // 计数器溢出时移动一次
      if (cnt == 24'd0) begin
        pattern <= {pattern[6:0], pattern[7]};  // 左移循环
      end
    end
  end

  assign led = pattern;

endmodule
