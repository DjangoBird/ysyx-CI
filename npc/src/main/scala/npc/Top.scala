package npc

import chisel3._

class Top extends RawModule {
  override def desiredName: String = "top"

  val clk = IO(Input(Clock()))
  val rst = IO(Input(Bool()))
  val commit_ready = IO(Input(Bool()))
  val led = IO(Output(UInt(8.W)))

  val imem_addr = IO(Output(UInt(32.W)))
  val imem_req_valid = IO(Output(Bool()))
  val imem_req_ready = IO(Input(Bool()))
  val imem_resp_valid = IO(Input(Bool()))
  val imem_resp_ready = IO(Output(Bool()))
  val imem_rdata = IO(Input(UInt(32.W)))

  val dmem_valid = IO(Output(Bool()))
  val dmem_req_ready = IO(Input(Bool()))
  val dmem_resp_valid = IO(Input(Bool()))
  val dmem_resp_ready = IO(Output(Bool()))
  val dmem_we = IO(Output(Bool()))
  val dmem_wmask = IO(Output(UInt(4.W)))
  val dmem_addr = IO(Output(UInt(32.W)))
  val dmem_wdata = IO(Output(UInt(32.W)))
  val dmem_rdata = IO(Input(UInt(32.W)))

  val trap = IO(Output(Bool()))
  val trap_code = IO(Output(UInt(32.W)))
  val commit_valid = IO(Output(Bool()))
  val commit_pc = IO(Output(UInt(32.W)))
  val commit_instr = IO(Output(UInt(32.W)))
  val commit_next_pc = IO(Output(UInt(32.W)))
  val commit_trap = IO(Output(Bool()))
  val commit_trap_code = IO(Output(UInt(32.W)))
  val commit_mem_valid = IO(Output(Bool()))
  val commit_mem_we = IO(Output(Bool()))
  val commit_mem_wmask = IO(Output(UInt(4.W)))
  val commit_mem_addr = IO(Output(UInt(32.W)))
  val commit_mem_wdata = IO(Output(UInt(32.W)))
  val commit_mem_rdata = IO(Output(UInt(32.W)))

  val dbg_x0_o = IO(Output(UInt(32.W)))
  val dbg_x1_o = IO(Output(UInt(32.W)))
  val dbg_x2_o = IO(Output(UInt(32.W)))
  val dbg_x3_o = IO(Output(UInt(32.W)))
  val dbg_x4_o = IO(Output(UInt(32.W)))
  val dbg_x5_o = IO(Output(UInt(32.W)))
  val dbg_x6_o = IO(Output(UInt(32.W)))
  val dbg_x7_o = IO(Output(UInt(32.W)))
  val dbg_x8_o = IO(Output(UInt(32.W)))
  val dbg_x9_o = IO(Output(UInt(32.W)))
  val dbg_x10_o = IO(Output(UInt(32.W)))
  val dbg_x11_o = IO(Output(UInt(32.W)))
  val dbg_x12_o = IO(Output(UInt(32.W)))
  val dbg_x13_o = IO(Output(UInt(32.W)))
  val dbg_x14_o = IO(Output(UInt(32.W)))
  val dbg_x15_o = IO(Output(UInt(32.W)))

  withClockAndReset(clk, rst.asAsyncReset) {
    val fetch = Module(new FetchStage)
    val decode = Module(new DecodeStage)
    val execute = Module(new ExecuteStage)
    val memory = Module(new MemoryStage)
    val writeback = Module(new WritebackStage)
    val regFile = Module(new RegisterFile)
    val csr = Module(new CsrFile)
    val pc = Module(new ProgramCounter)
    val traceDpi = Module(new TraceDpi)

    fetch.io.pc := pc.io.pc
    fetch.io.imemRdata := imem_rdata
    fetch.io.imemReqReady := imem_req_ready
    fetch.io.imemRespValid := imem_resp_valid
    imem_addr := fetch.io.imemAddr
    imem_req_valid := fetch.io.imemReqValid
    imem_resp_ready := fetch.io.imemRespReady
    StageConnect(fetch.io.out, decode.io.in)

    regFile.io.readAddr1 := decode.io.rs1Idx
    regFile.io.readAddr2 := decode.io.rs2Idx
    decode.io.rs1Val := regFile.io.readData1
    decode.io.rs2Val := regFile.io.readData2
    StageConnect(decode.io.out, execute.io.in)

    execute.io.a0Val := regFile.io.regs(10)
    execute.io.csrReadData := csr.io.readData
    execute.io.mtvec := csr.io.mtvec
    execute.io.mepc := csr.io.mepc
    StageConnect(execute.io.out, memory.io.in)

    memory.io.dmemRdata := dmem_rdata
    memory.io.dmemReqReady := dmem_req_ready
    memory.io.dmemRespValid := dmem_resp_valid
    StageConnect(memory.io.out, writeback.io.in)
    writeback.io.out.ready := commit_ready

    val executeFire = execute.io.out.fire
    val commit = writeback.io.out.fire

    csr.io.readAddr := execute.io.in.bits.immI(11, 0)
    csr.io.writeEnable := executeFire && execute.io.out.bits.csrWriteEnable
    csr.io.writeAddr := execute.io.out.bits.csrAddr
    csr.io.writeData := execute.io.out.bits.csrWriteData
    csr.io.ecall := executeFire && execute.io.out.bits.ecall
    csr.io.ecallPc := execute.io.in.bits.pc

    regFile.io.writeEnable := commit && writeback.io.out.bits.wbEn
    regFile.io.writeAddr := writeback.io.out.bits.wbIdx
    regFile.io.writeData := writeback.io.out.bits.wbData

    pc.io.enable := commit
    pc.io.nextPc := Mux(writeback.io.out.bits.trap, pc.io.pc, writeback.io.out.bits.pcNext)

    dmem_valid := memory.io.dmemReqValid
    dmem_resp_ready := memory.io.dmemRespReady
    dmem_we := memory.io.dmemReqWe
    dmem_wmask := memory.io.dmemReqWmask
    dmem_addr := memory.io.dmemReqAddr
    dmem_wdata := memory.io.dmemReqWdata

    val trapLatched = RegInit(false.B)
    val trapCodeLatched = RegInit(0.U(32.W))
    when(commit && writeback.io.out.bits.trap) {
      trapLatched := true.B
      trapCodeLatched := writeback.io.out.bits.trapCode
    }
    trap := trapLatched
    trap_code := trapCodeLatched
    commit_valid := commit
    commit_pc := pc.io.pc
    commit_instr := fetch.io.out.bits.instr
    commit_next_pc := writeback.io.out.bits.pcNext
    commit_trap := commit && writeback.io.out.bits.trap
    commit_trap_code := writeback.io.out.bits.trapCode
    commit_mem_valid := memory.io.commitMemValid
    commit_mem_we := execute.io.out.bits.dmemWe
    commit_mem_wmask := execute.io.out.bits.dmemWmask
    commit_mem_addr := execute.io.out.bits.dmemAddr
    commit_mem_wdata := execute.io.out.bits.dmemWdata
    commit_mem_rdata := dmem_rdata

    traceDpi.io.pc := pc.io.pc
    traceDpi.io.instr := fetch.io.out.bits.instr

    dbg_x0_o := regFile.io.regs(0)
    dbg_x1_o := regFile.io.regs(1)
    dbg_x2_o := regFile.io.regs(2)
    dbg_x3_o := regFile.io.regs(3)
    dbg_x4_o := regFile.io.regs(4)
    dbg_x5_o := regFile.io.regs(5)
    dbg_x6_o := regFile.io.regs(6)
    dbg_x7_o := regFile.io.regs(7)
    dbg_x8_o := regFile.io.regs(8)
    dbg_x9_o := regFile.io.regs(9)
    dbg_x10_o := regFile.io.regs(10)
    dbg_x11_o := regFile.io.regs(11)
    dbg_x12_o := regFile.io.regs(12)
    dbg_x13_o := regFile.io.regs(13)
    dbg_x14_o := regFile.io.regs(14)
    dbg_x15_o := regFile.io.regs(15)

    led := pc.io.pc(7, 0)
  }
}
