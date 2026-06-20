package npc

import chisel3._
import chisel3.util._

class CsrFile extends Module {
  val io = IO(new Bundle {
    val readAddr = Input(UInt(12.W))
    val readData = Output(UInt(32.W))
    val writeEnable = Input(Bool())
    val writeAddr = Input(UInt(12.W))
    val writeData = Input(UInt(32.W))
    val ecall = Input(Bool())
    val ecallPc = Input(UInt(32.W))
    val mtvec = Output(UInt(32.W))
    val mepc = Output(UInt(32.W))
  })

  val mcycle = RegInit(0.U(64.W))
  val mstatus = RegInit("h00001800".U(32.W))
  val mtvec = RegInit(0.U(32.W))
  val mepc = RegInit(0.U(32.W))
  val mcause = RegInit(0.U(32.W))

  mcycle := mcycle + 1.U

  when(io.writeEnable) {
    switch(io.writeAddr) {
      is("h300".U) { mstatus := io.writeData }
      is("h305".U) { mtvec := io.writeData }
      is("h341".U) { mepc := io.writeData }
      is("h342".U) { mcause := io.writeData }
    }
  }

  when(io.ecall) {
    mepc := io.ecallPc
    mcause := 11.U
  }

  io.readData := MuxLookup(io.readAddr, 0.U, Seq(
    "hB00".U -> mcycle(31, 0),
    "hB80".U -> mcycle(63, 32),
    "hF11".U -> "h79737978".U,
    "hF12".U -> 22040000.U,
    "h300".U -> mstatus,
    "h305".U -> mtvec,
    "h341".U -> mepc,
    "h342".U -> mcause
  ))

  io.mtvec := mtvec
  io.mepc := mepc
}
