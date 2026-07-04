package npc

import chisel3._

class Top extends RawModule {
  override def desiredName: String = "top"

  val clk = IO(Input(Clock()))
  val rst = IO(Input(Bool()))
  val commit_ready = IO(Input(Bool()))
  val led = IO(Output(UInt(8.W)))

  val ifu_axi_arvalid = IO(Output(Bool()))
  val ifu_axi_arready = IO(Input(Bool()))
  val ifu_axi_araddr = IO(Output(UInt(32.W)))
  val ifu_axi_rvalid = IO(Input(Bool()))
  val ifu_axi_rready = IO(Output(Bool()))
  val ifu_axi_rdata = IO(Input(UInt(32.W)))
  val ifu_axi_rresp = IO(Input(UInt(2.W)))
  val ifu_axi_awvalid = IO(Output(Bool()))
  val ifu_axi_awready = IO(Input(Bool()))
  val ifu_axi_awaddr = IO(Output(UInt(32.W)))
  val ifu_axi_wvalid = IO(Output(Bool()))
  val ifu_axi_wready = IO(Input(Bool()))
  val ifu_axi_wdata = IO(Output(UInt(32.W)))
  val ifu_axi_wstrb = IO(Output(UInt(4.W)))
  val ifu_axi_bvalid = IO(Input(Bool()))
  val ifu_axi_bready = IO(Output(Bool()))
  val ifu_axi_bresp = IO(Input(UInt(2.W)))

  val lsu_axi_arvalid = IO(Output(Bool()))
  val lsu_axi_arready = IO(Input(Bool()))
  val lsu_axi_araddr = IO(Output(UInt(32.W)))
  val lsu_axi_rvalid = IO(Input(Bool()))
  val lsu_axi_rready = IO(Output(Bool()))
  val lsu_axi_rdata = IO(Input(UInt(32.W)))
  val lsu_axi_rresp = IO(Input(UInt(2.W)))
  val lsu_axi_awvalid = IO(Output(Bool()))
  val lsu_axi_awready = IO(Input(Bool()))
  val lsu_axi_awaddr = IO(Output(UInt(32.W)))
  val lsu_axi_wvalid = IO(Output(Bool()))
  val lsu_axi_wready = IO(Input(Bool()))
  val lsu_axi_wdata = IO(Output(UInt(32.W)))
  val lsu_axi_wstrb = IO(Output(UInt(4.W)))
  val lsu_axi_bvalid = IO(Input(Bool()))
  val lsu_axi_bready = IO(Output(Bool()))
  val lsu_axi_bresp = IO(Input(UInt(2.W)))

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
    fetch.io.axiArReady := ifu_axi_arready
    fetch.io.axiRValid := ifu_axi_rvalid
    fetch.io.axiRData := ifu_axi_rdata
    fetch.io.axiRResp := ifu_axi_rresp
    ifu_axi_arvalid := fetch.io.axiArValid
    ifu_axi_araddr := fetch.io.axiArAddr
    ifu_axi_rready := fetch.io.axiRReady
    ifu_axi_awvalid := false.B
    ifu_axi_awaddr := 0.U
    ifu_axi_wvalid := false.B
    ifu_axi_wdata := 0.U
    ifu_axi_wstrb := 0.U
    ifu_axi_bready := false.B
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

    memory.io.axiArReady := lsu_axi_arready
    memory.io.axiRValid := lsu_axi_rvalid
    memory.io.axiRData := lsu_axi_rdata
    memory.io.axiRResp := lsu_axi_rresp
    memory.io.axiAwReady := lsu_axi_awready
    memory.io.axiWReady := lsu_axi_wready
    memory.io.axiBValid := lsu_axi_bvalid
    memory.io.axiBResp := lsu_axi_bresp
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

    lsu_axi_arvalid := memory.io.axiArValid
    lsu_axi_araddr := memory.io.axiArAddr
    lsu_axi_rready := memory.io.axiRReady
    lsu_axi_awvalid := memory.io.axiAwValid
    lsu_axi_awaddr := memory.io.axiAwAddr
    lsu_axi_wvalid := memory.io.axiWValid
    lsu_axi_wdata := memory.io.axiWData
    lsu_axi_wstrb := memory.io.axiWStrb
    lsu_axi_bready := memory.io.axiBReady

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
    commit_mem_rdata := lsu_axi_rdata

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
