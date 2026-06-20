package npc

import chisel3._

class ProgramCounter extends Module {
  val io = IO(new Bundle {
    val nextPc = Input(UInt(32.W))
    val enable = Input(Bool())
    val pc = Output(UInt(32.W))
  })

  val pcReg = RegInit(Rv32e.ResetPc)
  when(io.enable) {
    pcReg := io.nextPc
  }
  io.pc := pcReg
}
