package npc

import chisel3._
import chisel3.util._

class DecodeStage extends Module {
  val io = IO(new Bundle {
    val in = Flipped(Decoupled(new FetchMessage))
    val rs1Idx = Output(UInt(4.W))
    val rs2Idx = Output(UInt(4.W))
    val rs1Val = Input(UInt(32.W))
    val rs2Val = Input(UInt(32.W))
    val out = Decoupled(new DecodeMessage)
  })

  val instr = io.in.bits.instr
  val rdRaw = instr(11, 7)
  val rs1Raw = instr(19, 15)
  val rs2Raw = instr(24, 20)

  io.in.ready := io.out.ready
  io.out.valid := io.in.valid
  io.rs1Idx := rs1Raw(3, 0)
  io.rs2Idx := rs2Raw(3, 0)

  io.out.bits.pc := io.in.bits.pc
  io.out.bits.pcNextSeq := io.in.bits.pcNextSeq
  io.out.bits.opcode := instr(6, 0)
  io.out.bits.rdRaw := rdRaw
  io.out.bits.funct3 := instr(14, 12)
  io.out.bits.rs1Raw := rs1Raw
  io.out.bits.rs2Raw := rs2Raw
  io.out.bits.funct7 := instr(31, 25)
  io.out.bits.rdIdx := rdRaw(3, 0)
  io.out.bits.rs1Val := io.rs1Val
  io.out.bits.rs2Val := io.rs2Val
  io.out.bits.immI := Rv32e.sext(instr(31, 20), 12)
  io.out.bits.immS := Rv32e.sext(Cat(instr(31, 25), instr(11, 7)), 12)
  io.out.bits.immB := Rv32e.sext(Cat(instr(31), instr(7), instr(30, 25), instr(11, 8), 0.U(1.W)), 13)
  io.out.bits.immU := Cat(instr(31, 12), 0.U(12.W))
  io.out.bits.immJ := Rv32e.sext(Cat(instr(31), instr(19, 12), instr(20), instr(30, 21), 0.U(1.W)), 21)

  io.out.bits.isEbreak := io.out.bits.opcode === Rv32e.Opcode.SYSTEM &&
    io.out.bits.funct3 === 0.U &&
    rs1Raw === 0.U &&
    rdRaw === 0.U &&
    instr(31, 20) === 1.U
}
