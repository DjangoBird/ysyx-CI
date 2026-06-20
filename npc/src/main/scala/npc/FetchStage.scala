package npc

import chisel3._
import chisel3.util._

class FetchStage extends Module {
  val io = IO(new Bundle {
    val pc = Input(UInt(32.W))
    val imemRdata = Input(UInt(32.W))
    val imemAddr = Output(UInt(32.W))
    val out = Decoupled(new FetchMessage)
  })

  val idle :: waitResponse :: Nil = Enum(2)
  val state = RegInit(idle)

  switch(state) {
    is(idle) {
      state := waitResponse
    }
    is(waitResponse) {
      when(io.out.fire) {
        state := idle
      }
    }
  }

  io.imemAddr := io.pc
  io.out.valid := state === waitResponse
  io.out.bits.pc := io.pc
  io.out.bits.instr := io.imemRdata
  io.out.bits.pcNextSeq := io.pc + 4.U
}
