package npc

import chisel3._
import chisel3.util._

class FetchStage extends Module {
  val io = IO(new Bundle {
    val pc = Input(UInt(32.W))
    val imemRdata = Input(UInt(32.W))
    val imemAddr = Output(UInt(32.W))
    val imemReqValid = Output(Bool())
    val imemReqReady = Input(Bool())
    val imemRespValid = Input(Bool())
    val imemRespReady = Output(Bool())
    val out = Decoupled(new FetchMessage)
  })

  val request :: response :: Nil = Enum(2)
  val state = RegInit(request)

  switch(state) {
    is(request) {
      when(io.imemReqValid && io.imemReqReady) {
        state := response
      }
    }
    is(response) {
      when(io.imemRespValid && io.imemRespReady) {
        state := request
      }
    }
  }

  io.imemAddr := io.pc
  io.imemReqValid := state === request
  io.imemRespReady := state === response && io.out.ready
  io.out.valid := state === response && io.imemRespValid
  io.out.bits.pc := io.pc
  io.out.bits.instr := io.imemRdata
  io.out.bits.pcNextSeq := io.pc + 4.U
}
