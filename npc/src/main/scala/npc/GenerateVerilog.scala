package npc

import chisel3.stage.ChiselStage

object GenerateVerilog extends App {
  (new ChiselStage).emitVerilog(
    new Top,
    Array("--target-dir", "build/chisel")
  )
}