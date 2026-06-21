package npc

import chisel3._

class RegisterFile extends Module {
  val io = IO(new Bundle {
    val readAddr1 = Input(UInt(4.W))
    val readAddr2 = Input(UInt(4.W))
    val readData1 = Output(UInt(32.W))
    val readData2 = Output(UInt(32.W))
    val writeEnable = Input(Bool())
    val writeAddr = Input(UInt(4.W))
    val writeData = Input(UInt(32.W))
    val regs = Output(Vec(Rv32e.RegCount, UInt(32.W)))
  })

  val regs = RegInit(VecInit(Seq.fill(Rv32e.RegCount)(0.U(32.W))))

  io.readData1 := regs(io.readAddr1)
  io.readData2 := regs(io.readAddr2)
  io.regs := regs

  when(io.writeEnable && io.writeAddr =/= 0.U) {
    regs(io.writeAddr) := io.writeData
  }
}