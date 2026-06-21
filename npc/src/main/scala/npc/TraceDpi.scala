package npc

import chisel3._
import chisel3.util.HasBlackBoxInline

class TraceDpi extends BlackBox with HasBlackBoxInline {
  val io = IO(new Bundle {
    val pc = Input(UInt(32.W))
    val instr = Input(UInt(32.W))
  })

  setInline("TraceDpi.v",
    """
module TraceDpi(
  input [31:0] pc,
  input [31:0] instr
);
  import "DPI-C" function void npc_set_current_instr(input int pc, input int instr);
  always @(*) begin
    npc_set_current_instr(pc, instr);
  end
endmodule
""")
}
