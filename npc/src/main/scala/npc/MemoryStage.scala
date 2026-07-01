package npc

import chisel3._
import chisel3.util._

class MemoryStage extends Module {
  val io = IO(new Bundle {
    val in = Flipped(Decoupled(new ExecuteOut))
    val out = Decoupled(new WritebackMessage)

    val axiArValid = Output(Bool())
    val axiArReady = Input(Bool())
    val axiArAddr = Output(UInt(32.W))
    val axiRValid = Input(Bool())
    val axiRReady = Output(Bool())
    val axiRData = Input(UInt(32.W))
    val axiRResp = Input(UInt(2.W))

    val axiAwValid = Output(Bool())
    val axiAwReady = Input(Bool())
    val axiAwAddr = Output(UInt(32.W))
    val axiWValid = Output(Bool())
    val axiWReady = Input(Bool())
    val axiWData = Output(UInt(32.W))
    val axiWStrb = Output(UInt(4.W))
    val axiBValid = Input(Bool())
    val axiBReady = Output(Bool())
    val axiBResp = Input(UInt(2.W))

    val commitMemValid = Output(Bool())
  })

  val idle :: readResponse :: writeRequest :: writeResponse :: Nil = Enum(4)
  val state = RegInit(idle)
  val awDone = RegInit(false.B)
  val wDone = RegInit(false.B)
  val memoryOperation = io.in.valid && io.in.bits.dmemValid
  val loadOperation = memoryOperation && !io.in.bits.dmemWe
  val storeOperation = memoryOperation && io.in.bits.dmemWe
  val awFire = io.axiAwValid && io.axiAwReady
  val wFire = io.axiWValid && io.axiWReady

  switch(state) {
    is(idle) {
      when(loadOperation && io.axiArValid && io.axiArReady) {
        state := readResponse
      }.elsewhen(storeOperation) {
        state := writeRequest
        awDone := false.B
        wDone := false.B
      }
    }
    is(readResponse) {
      when(io.axiRValid && io.axiRReady) { state := idle }
    }
    is(writeRequest) {
      when(awFire) { awDone := true.B }
      when(wFire) { wDone := true.B }
      when((awDone || awFire) && (wDone || wFire)) {
        state := writeResponse
      }
    }
    is(writeResponse) {
      when(io.axiBValid && io.axiBReady) { state := idle }
    }
  }

  val loadByte = MuxLookup(io.in.bits.loadByteOff, 0.U, Seq(
    0.U -> io.axiRData(7, 0),
    1.U -> io.axiRData(15, 8),
    2.U -> io.axiRData(23, 16),
    3.U -> io.axiRData(31, 24)
  ))
  val loadHalf = Mux(io.in.bits.loadByteOff(1), io.axiRData(31, 16), io.axiRData(15, 0))
  val wbData = Mux(!io.in.bits.wbFromMem, io.in.bits.wbDataPre,
    MuxLookup(io.in.bits.loadFunct3, io.in.bits.wbDataPre, Seq(
      Rv32e.Funct3.LB -> Rv32e.sext(loadByte, 8),
      Rv32e.Funct3.LH -> Rv32e.sext(loadHalf, 16),
      Rv32e.Funct3.LW -> io.axiRData,
      Rv32e.Funct3.LBU -> Cat(0.U(24.W), loadByte),
      Rv32e.Funct3.LHU -> Cat(0.U(16.W), loadHalf)
    )))

  io.in.ready := MuxLookup(state, false.B, Seq(
    idle -> (!io.in.bits.dmemValid && io.out.ready),
    readResponse -> (io.axiRValid && io.out.ready),
    writeResponse -> (io.axiBValid && io.out.ready)
  ))
  io.out.valid := io.in.valid && (
    (state === readResponse && io.axiRValid) ||
    (state === writeResponse && io.axiBValid) ||
    (state === idle && !io.in.bits.dmemValid))
  io.out.bits.wbEn := io.in.bits.wbEn
  io.out.bits.wbIdx := io.in.bits.wbIdx
  io.out.bits.wbData := wbData
  io.out.bits.pcNext := io.in.bits.pcNext
  io.out.bits.trap := io.in.bits.trap
  io.out.bits.trapCode := io.in.bits.trapCode

  io.axiArValid := loadOperation && state === idle
  io.axiArAddr := io.in.bits.dmemAddr
  io.axiRReady := io.in.valid && state === readResponse && io.out.ready
  io.axiAwValid := io.in.valid && state === writeRequest && !awDone
  io.axiAwAddr := io.in.bits.dmemAddr
  io.axiWValid := io.in.valid && state === writeRequest && !wDone
  io.axiWData := io.in.bits.dmemWdata
  io.axiWStrb := io.in.bits.dmemWmask
  io.axiBReady := io.in.valid && state === writeResponse && io.out.ready

  io.commitMemValid := memoryOperation && (
    (state === readResponse && io.axiRValid && io.out.ready) ||
    (state === writeResponse && io.axiBValid && io.out.ready))
}
