`ifndef MINIRV_DEFS_VH
`define MINIRV_DEFS_VH

`define OPCODE_OP      7'b0110011
`define OPCODE_OP_IMM  7'b0010011
`define OPCODE_AUIPC   7'b0010111
`define OPCODE_LUI     7'b0110111
`define OPCODE_JAL     7'b1101111
`define OPCODE_LOAD    7'b0000011
`define OPCODE_STORE   7'b0100011
`define OPCODE_MISC_MEM 7'b0001111
`define OPCODE_BRANCH  7'b1100011
`define OPCODE_JALR    7'b1100111
`define OPCODE_SYSTEM  7'b1110011

`define F3_ADD_SUB 3'b000
`define F3_SLL     3'b001
`define F3_SLT     3'b010
`define F3_SLTU    3'b011
`define F3_XOR     3'b100
`define F3_SRL_SRA 3'b101
`define F3_OR      3'b110
`define F3_AND     3'b111

`define F3_BEQ     3'b000
`define F3_BNE     3'b001
`define F3_BLT     3'b100
`define F3_BGE     3'b101
`define F3_BLTU    3'b110
`define F3_BGEU    3'b111

`define F3_LB      3'b000
`define F3_LH      3'b001
`define F3_LW      3'b010
`define F3_LBU     3'b100
`define F3_LHU     3'b101
`define F3_SW      3'b010
`define F3_SB      3'b000
`define F3_SH      3'b001

`endif
