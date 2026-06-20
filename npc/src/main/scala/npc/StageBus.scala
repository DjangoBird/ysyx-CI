package npc

import chisel3._
import chisel3.util._

class FetchMessage extends Bundle {
  val pc = UInt(32.W)
  val instr = UInt(32.W)
  val pcNextSeq = UInt(32.W)
}

class DecodeMessage extends Bundle {
  val pc = UInt(32.W)
  val pcNextSeq = UInt(32.W)
  val opcode = UInt(7.W)
  val rdRaw = UInt(5.W)
  val funct3 = UInt(3.W)
  val rs1Raw = UInt(5.W)
  val rs2Raw = UInt(5.W)
  val funct7 = UInt(7.W)
  val rdIdx = UInt(4.W)
  val rs1Val = UInt(32.W)
  val rs2Val = UInt(32.W)
  val immI = UInt(32.W)
  val immS = UInt(32.W)
  val immB = UInt(32.W)
  val immU = UInt(32.W)
  val immJ = UInt(32.W)
  val isEbreak = Bool()
}

class WritebackMessage extends Bundle {
  val wbEn = Bool()
  val wbIdx = UInt(4.W)
  val wbData = UInt(32.W)
  val pcNext = UInt(32.W)
  val trap = Bool()
  val trapCode = UInt(32.W)
}

object StageConnect {
  def apply[T <: Data](left: DecoupledIO[T], right: DecoupledIO[T]): Unit = {
    right.bits := left.bits
    right.valid := left.valid
    left.ready := right.ready
  }
}
