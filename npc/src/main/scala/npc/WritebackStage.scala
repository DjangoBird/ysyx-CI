package npc

import chisel3._
import chisel3.util._

class WritebackStage extends Module {
  val io = IO(new Bundle {
    val in = Flipped(Decoupled(new WritebackMessage))
    val out = Decoupled(new WritebackMessage)
  })

  StageConnect(io.in, io.out)
}
