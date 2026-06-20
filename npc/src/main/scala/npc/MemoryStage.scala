package npc

import chisel3._
import chisel3.util._

class MemoryStage extends Module {
  val io = IO(new Bundle {
    val in = Flipped(Decoupled(new ExecuteOut))
    val dmemRdata = Input(UInt(32.W))
    val out = Decoupled(new WritebackMessage)
  })

  val loadByte = MuxLookup(io.in.bits.loadByteOff, 0.U, Seq(
    0.U -> io.dmemRdata(7, 0),
    1.U -> io.dmemRdata(15, 8),
    2.U -> io.dmemRdata(23, 16),
    3.U -> io.dmemRdata(31, 24)
  ))
  val loadHalf = Mux(io.in.bits.loadByteOff(1), io.dmemRdata(31, 16), io.dmemRdata(15, 0))

  val wbData = Mux(
    !io.in.bits.wbFromMem,
    io.in.bits.wbDataPre,
    MuxLookup(io.in.bits.loadFunct3, io.in.bits.wbDataPre, Seq(
      Rv32e.Funct3.LB -> Rv32e.sext(loadByte, 8),
      Rv32e.Funct3.LH -> Rv32e.sext(loadHalf, 16),
      Rv32e.Funct3.LW -> io.dmemRdata,
      Rv32e.Funct3.LBU -> Cat(0.U(24.W), loadByte),
      Rv32e.Funct3.LHU -> Cat(0.U(16.W), loadHalf)
    ))
  )

  io.in.ready := io.out.ready
  io.out.valid := io.in.valid
  io.out.bits.wbEn := io.in.bits.wbEn
  io.out.bits.wbIdx := io.in.bits.wbIdx
  io.out.bits.wbData := wbData
  io.out.bits.pcNext := io.in.bits.pcNext
  io.out.bits.trap := io.in.bits.trap
  io.out.bits.trapCode := io.in.bits.trapCode
}
