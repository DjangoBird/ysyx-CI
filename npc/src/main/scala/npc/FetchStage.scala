package npc

import chisel3._
import chisel3.util._

class FetchStage extends Module {
  val io = IO(new Bundle {
    val pc = Input(UInt(32.W))
    val axiArValid = Output(Bool())
    val axiArReady = Input(Bool())
    val axiArAddr = Output(UInt(32.W))
    val axiRValid = Input(Bool())
    val axiRReady = Output(Bool())
    val axiRData = Input(UInt(32.W))
    val axiRResp = Input(UInt(2.W))
    val out = Decoupled(new FetchMessage)
  })

  val request :: response :: output :: Nil = Enum(3)
  val state = RegInit(request)
  val instrReg = RegInit(0.U(32.W))
  val respReg = RegInit(0.U(2.W))

  switch(state) {
    is(request) {
      when(io.axiArValid && io.axiArReady) {
        state := response
      }
    }
    is(response) {
      when(io.axiRValid && io.axiRReady) {
        instrReg := io.axiRData
        respReg := io.axiRResp
        state := output
      }
    }
    is(output) {
      when(io.out.fire) {
        state := request
      }
    }
  }

  io.axiArValid := state === request
  io.axiArAddr := io.pc
  io.axiRReady := state === response
  io.out.valid := state === output
  io.out.bits.pc := io.pc
  io.out.bits.instr := instrReg
  io.out.bits.pcNextSeq := io.pc + 4.U

  dontTouch(respReg)
}
