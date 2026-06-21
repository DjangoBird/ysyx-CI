package npc

import chisel3._
import chisel3.util._

class ExecuteOut extends Bundle {
  val wbEn = Bool()
  val wbIdx = UInt(4.W)
  val wbDataPre = UInt(32.W)
  val wbFromMem = Bool()
  val dmemValid = Bool()
  val dmemWe = Bool()
  val dmemWmask = UInt(4.W)
  val dmemAddr = UInt(32.W)
  val dmemWdata = UInt(32.W)
  val loadByteOff = UInt(2.W)
  val loadFunct3 = UInt(3.W)
  val pcNext = UInt(32.W)
  val trap = Bool()
  val trapCode = UInt(32.W)
  val csrAddr = UInt(12.W)
  val csrWriteEnable = Bool()
  val csrWriteData = UInt(32.W)
  val ecall = Bool()
}

class ExecuteStage extends Module {
  val io = IO(new Bundle {
    val in = Flipped(Decoupled(new DecodeMessage))
    val a0Val = Input(UInt(32.W))
    val csrReadData = Input(UInt(32.W))
    val mtvec = Input(UInt(32.W))
    val mepc = Input(UInt(32.W))
    val out = Decoupled(new ExecuteOut)

    def opcode = in.bits.opcode
    def rdRaw = in.bits.rdRaw
    def funct3 = in.bits.funct3
    def rs1Raw = in.bits.rs1Raw
    def rs2Raw = in.bits.rs2Raw
    def funct7 = in.bits.funct7
    def rdIdx = in.bits.rdIdx
    def rs1Val = in.bits.rs1Val
    def rs2Val = in.bits.rs2Val
    def immI = in.bits.immI
    def immS = in.bits.immS
    def immB = in.bits.immB
    def immU = in.bits.immU
    def immJ = in.bits.immJ
    def pc = in.bits.pc
    def pcNextSeq = in.bits.pcNextSeq
    def isEbreak = in.bits.isEbreak
  })

  val loadAddr = io.rs1Val + io.immI
  val storeAddr = io.rs1Val + io.immS
  val loadWordAddr = Cat(loadAddr(31, 2), 0.U(2.W))
  val storeWordAddr = Cat(storeAddr(31, 2), 0.U(2.W))
  val shamtI = io.immI(4, 0)
  val shamtR = io.rs2Val(4, 0)
  val isEcall = io.opcode === Rv32e.Opcode.SYSTEM &&
    io.funct3 === 0.U && io.rs1Raw === 0.U && io.rdRaw === 0.U && io.immI(11, 0) === 0.U
  val isMret = io.opcode === Rv32e.Opcode.SYSTEM &&
    io.funct3 === 0.U && io.rs1Raw === 0.U && io.rdRaw === 0.U && io.immI(11, 0) === "h302".U
  def sll32(value: UInt, shamt: UInt): UInt = {
    MuxLookup(shamt, 0.U(32.W))((0 until 32).map { i =>
      i.U -> (if (i == 0) value else Cat(value(31 - i, 0), 0.U(i.W)))
    })
  }

  val ctrl = WireDefault(0.U.asTypeOf(new ExecuteOut))
  ctrl.wbIdx := io.rdIdx
  ctrl.loadByteOff := loadAddr(1, 0)
  ctrl.loadFunct3 := io.funct3
  ctrl.pcNext := io.pcNextSeq
  ctrl.csrAddr := io.immI(11, 0)

  when(io.opcode === Rv32e.Opcode.OP &&
    io.rdRaw < 16.U && io.rs1Raw < 16.U && io.rs2Raw < 16.U) {
    ctrl.wbEn := io.rdIdx =/= 0.U
    switch(io.funct3) {
      is(Rv32e.Funct3.ADD_SUB) {
        when(io.funct7 === "b0100000".U) {
          ctrl.wbDataPre := io.rs1Val - io.rs2Val
        }.elsewhen(io.funct7 === 0.U) {
          ctrl.wbDataPre := io.rs1Val + io.rs2Val
        }.otherwise {
          ctrl.wbEn := false.B
        }
      }
      is(Rv32e.Funct3.SLL) {
        when(io.funct7 === 0.U) {
          ctrl.wbDataPre := sll32(io.rs1Val, shamtR)
        }.otherwise {
          ctrl.wbEn := false.B
        }
      }
      is(Rv32e.Funct3.SLT) {
        when(io.funct7 === 0.U) {
          ctrl.wbDataPre := (io.rs1Val.asSInt < io.rs2Val.asSInt).asUInt
        }.otherwise {
          ctrl.wbEn := false.B
        }
      }
      is(Rv32e.Funct3.SLTU) {
        when(io.funct7 === 0.U) {
          ctrl.wbDataPre := (io.rs1Val < io.rs2Val).asUInt
        }.otherwise {
          ctrl.wbEn := false.B
        }
      }
      is(Rv32e.Funct3.XOR) {
        when(io.funct7 === 0.U) {
          ctrl.wbDataPre := io.rs1Val ^ io.rs2Val
        }.otherwise {
          ctrl.wbEn := false.B
        }
      }
      is(Rv32e.Funct3.SRL_SRA) {
        when(io.funct7 === "b0100000".U) {
          ctrl.wbDataPre := (io.rs1Val.asSInt >> shamtR).asUInt
        }.elsewhen(io.funct7 === 0.U) {
          ctrl.wbDataPre := io.rs1Val >> shamtR
        }.otherwise {
          ctrl.wbEn := false.B
        }
      }
      is(Rv32e.Funct3.OR) {
        when(io.funct7 === 0.U) {
          ctrl.wbDataPre := io.rs1Val | io.rs2Val
        }.otherwise {
          ctrl.wbEn := false.B
        }
      }
      is(Rv32e.Funct3.AND) {
        when(io.funct7 === 0.U) {
          ctrl.wbDataPre := io.rs1Val & io.rs2Val
        }.otherwise {
          ctrl.wbEn := false.B
        }
      }
    }
  }.elsewhen(io.opcode === Rv32e.Opcode.OP_IMM &&
    io.rdRaw < 16.U && io.rs1Raw < 16.U) {
    ctrl.wbEn := io.rdIdx =/= 0.U
    switch(io.funct3) {
      is(Rv32e.Funct3.ADD_SUB) {
        ctrl.wbDataPre := io.rs1Val + io.immI
      }
      is(Rv32e.Funct3.SLL) {
        when(io.immI(11, 5) === 0.U) {
          ctrl.wbDataPre := sll32(io.rs1Val, shamtI)
        }.otherwise {
          ctrl.wbEn := false.B
        }
      }
      is(Rv32e.Funct3.SLT) {
        ctrl.wbDataPre := (io.rs1Val.asSInt < io.immI.asSInt).asUInt
      }
      is(Rv32e.Funct3.SLTU) {
        ctrl.wbDataPre := (io.rs1Val < io.immI).asUInt
      }
      is(Rv32e.Funct3.XOR) {
        ctrl.wbDataPre := io.rs1Val ^ io.immI
      }
      is(Rv32e.Funct3.SRL_SRA) {
        when(io.immI(11, 5) === "b0100000".U) {
          ctrl.wbDataPre := (io.rs1Val.asSInt >> shamtI).asUInt
        }.elsewhen(io.immI(11, 5) === 0.U) {
          ctrl.wbDataPre := io.rs1Val >> shamtI
        }.otherwise {
          ctrl.wbEn := false.B
        }
      }
      is(Rv32e.Funct3.OR) {
        ctrl.wbDataPre := io.rs1Val | io.immI
      }
      is(Rv32e.Funct3.AND) {
        ctrl.wbDataPre := io.rs1Val & io.immI
      }
    }
  }.elsewhen(io.opcode === Rv32e.Opcode.LUI && io.rdRaw < 16.U) {
    ctrl.wbEn := io.rdIdx =/= 0.U
    ctrl.wbDataPre := io.immU
  }.elsewhen(io.opcode === Rv32e.Opcode.AUIPC && io.rdRaw < 16.U) {
    ctrl.wbEn := io.rdIdx =/= 0.U
    ctrl.wbDataPre := io.pc + io.immU
  }.elsewhen(io.opcode === Rv32e.Opcode.LOAD && io.rdRaw < 16.U && io.rs1Raw < 16.U) {
    ctrl.dmemValid := true.B
    ctrl.dmemAddr := loadWordAddr
    ctrl.wbEn := io.rdIdx =/= 0.U
    ctrl.wbFromMem := true.B
  }.elsewhen(io.opcode === Rv32e.Opcode.STORE && io.rs1Raw < 16.U && io.rs2Raw < 16.U) {
    ctrl.dmemValid := true.B
    ctrl.dmemWe := true.B
    ctrl.dmemAddr := storeWordAddr

    when(io.funct3 === Rv32e.Funct3.SW) {
      ctrl.dmemWmask := "b1111".U
      ctrl.dmemWdata := io.rs2Val
    }.elsewhen(io.funct3 === Rv32e.Funct3.SH) {
      when(storeAddr(1)) {
        ctrl.dmemWmask := "b1100".U
        ctrl.dmemWdata := Cat(io.rs2Val(15, 0), 0.U(16.W))
      }.otherwise {
        ctrl.dmemWmask := "b0011".U
        ctrl.dmemWdata := Cat(0.U(16.W), io.rs2Val(15, 0))
      }
    }.elsewhen(io.funct3 === Rv32e.Funct3.SB) {
      switch(storeAddr(1, 0)) {
        is(0.U) {
          ctrl.dmemWmask := "b0001".U
          ctrl.dmemWdata := Cat(0.U(24.W), io.rs2Val(7, 0))
        }
        is(1.U) {
          ctrl.dmemWmask := "b0010".U
          ctrl.dmemWdata := Cat(0.U(16.W), io.rs2Val(7, 0), 0.U(8.W))
        }
        is(2.U) {
          ctrl.dmemWmask := "b0100".U
          ctrl.dmemWdata := Cat(0.U(8.W), io.rs2Val(7, 0), 0.U(16.W))
        }
        is(3.U) {
          ctrl.dmemWmask := "b1000".U
          ctrl.dmemWdata := Cat(io.rs2Val(7, 0), 0.U(24.W))
        }
      }
    }.otherwise {
        ctrl.dmemValid := false.B
        ctrl.dmemWe := false.B
        ctrl.dmemWmask := 0.U
    }
  }.elsewhen(io.opcode === Rv32e.Opcode.BRANCH && io.rs1Raw < 16.U && io.rs2Raw < 16.U) {
    switch(io.funct3) {
      is(Rv32e.Funct3.BEQ) {
        when(io.rs1Val === io.rs2Val) { ctrl.pcNext := io.pc + io.immB }
      }
      is(Rv32e.Funct3.BNE) {
        when(io.rs1Val =/= io.rs2Val) { ctrl.pcNext := io.pc + io.immB }
      }
      is(Rv32e.Funct3.BLT) {
        when(io.rs1Val.asSInt < io.rs2Val.asSInt) { ctrl.pcNext := io.pc + io.immB }
      }
      is(Rv32e.Funct3.BGE) {
        when(io.rs1Val.asSInt >= io.rs2Val.asSInt) { ctrl.pcNext := io.pc + io.immB }
      }
      is(Rv32e.Funct3.BLTU) {
        when(io.rs1Val < io.rs2Val) { ctrl.pcNext := io.pc + io.immB }
      }
      is(Rv32e.Funct3.BGEU) {
        when(io.rs1Val >= io.rs2Val) { ctrl.pcNext := io.pc + io.immB }
      }
    }
  }.elsewhen(io.opcode === Rv32e.Opcode.MISC_MEM && io.rdRaw === 0.U && io.rs1Raw === 0.U) {
    ctrl.pcNext := io.pcNextSeq
  }.elsewhen(io.opcode === Rv32e.Opcode.JALR &&
    io.rdRaw < 16.U && io.rs1Raw < 16.U && io.funct3 === 0.U) {
    ctrl.wbEn := io.rdIdx =/= 0.U
    ctrl.wbDataPre := io.pcNextSeq
    ctrl.pcNext := (io.rs1Val + io.immI) & "hfffffffe".U
  }.elsewhen(io.opcode === Rv32e.Opcode.JAL && io.rdRaw < 16.U) {
    ctrl.wbEn := io.rdIdx =/= 0.U
    ctrl.wbDataPre := io.pcNextSeq
    ctrl.pcNext := io.pc + io.immJ
  }.elsewhen(io.opcode === Rv32e.Opcode.SYSTEM && io.isEbreak) {
    ctrl.trap := true.B
    ctrl.trapCode := io.a0Val
    ctrl.pcNext := io.pc
  }.elsewhen(isEcall) {
    ctrl.ecall := true.B
    ctrl.pcNext := io.mtvec
  }.elsewhen(isMret) {
    ctrl.pcNext := io.mepc
  }.elsewhen(io.opcode === Rv32e.Opcode.SYSTEM &&
    (io.funct3 === "b001".U || io.funct3 === "b010".U) &&
    io.rdRaw < 16.U && io.rs1Raw < 16.U) {
    ctrl.wbEn := io.rdIdx =/= 0.U
    ctrl.wbDataPre := io.csrReadData
    when(io.funct3 === "b001".U) {
      ctrl.csrWriteEnable := true.B
      ctrl.csrWriteData := io.rs1Val
    }.otherwise {
      ctrl.csrWriteEnable := io.rs1Raw =/= 0.U
      ctrl.csrWriteData := io.csrReadData | io.rs1Val
    }
  }

  io.in.ready := io.out.ready
  io.out.valid := io.in.valid
  io.out.bits := ctrl
}
