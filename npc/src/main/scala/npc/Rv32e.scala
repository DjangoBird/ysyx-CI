package npc

import chisel3._
import chisel3.util._

object Rv32e {
  val ResetPc = "h80000000".U(32.W)
  val RegCount = 16

  object Opcode {
    val OP = "b0110011".U(7.W)
    val OP_IMM = "b0010011".U(7.W)
    val AUIPC = "b0010111".U(7.W)
    val LUI = "b0110111".U(7.W)
    val LOAD = "b0000011".U(7.W)
    val STORE = "b0100011".U(7.W)
    val MISC_MEM = "b0001111".U(7.W)
    val BRANCH = "b1100011".U(7.W)
    val JAL = "b1101111".U(7.W)
    val JALR = "b1100111".U(7.W)
    val SYSTEM = "b1110011".U(7.W)
  }

  object Funct3 {
    val ADD_SUB = "b000".U(3.W)
    val SLL = "b001".U(3.W)
    val SLT = "b010".U(3.W)
    val SLTU = "b011".U(3.W)
    val XOR = "b100".U(3.W)
    val SRL_SRA = "b101".U(3.W)
    val OR = "b110".U(3.W)
    val AND = "b111".U(3.W)
    val BEQ = "b000".U(3.W)
    val BNE = "b001".U(3.W)
    val BLT = "b100".U(3.W)
    val BGE = "b101".U(3.W)
    val BLTU = "b110".U(3.W)
    val BGEU = "b111".U(3.W)
    val LB = "b000".U(3.W)
    val LH = "b001".U(3.W)
    val SW = "b010".U(3.W)
    val SB = "b000".U(3.W)
    val SH = "b001".U(3.W)
    val LW = "b010".U(3.W)
    val LBU = "b100".U(3.W)
    val LHU = "b101".U(3.W)
  }

  def sext(value: UInt, fromBits: Int): UInt = {
    Cat(Fill(32 - fromBits, value(fromBits - 1)), value(fromBits - 1, 0))
  }
}
