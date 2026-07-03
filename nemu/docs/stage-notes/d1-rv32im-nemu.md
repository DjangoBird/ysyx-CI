# D1：支持 RV32IM 的 NEMU

## 学习记录

D1 的核心是把 PA2.1 的“取指、译码、执行、更新 PC”模型落实到 RV32IM NEMU。
这一步不是为了写出很多模块，而是先让 NEMU 形成稳定的解释执行闭环。

重点理解：

- `pc/snpc/dnpc` 的区别。
- 普通指令和跳转/异常指令都通过 `dnpc` 更新下一 PC。
- `x0` 必须在每条指令后保持 0。
- RV32M 的除零和溢出行为要符合 RISC-V 规范。

## 实现记录

主要代码：

```text
nemu/src/isa/riscv32/inst.c
nemu/src/isa/riscv32/system/intr.c
nemu/src/isa/riscv32/include/isa-def.h
nemu/src/cpu/cpu-exec.c
```

已实现：

- RV32I：整数、访存、跳转、分支、系统指令。
- RV32M：`mul/mulh/mulhsu/mulhu/div/divu/rem/remu`。
- CSR：`mstatus/mtvec/mepc/mcause/mvendorid/marchid`。
- `ecall/mret/ebreak`。

## 关键代码与讲解

取指入口：

```c
int isa_exec_once(Decode *s) {
  s->isa.inst = inst_fetch(&s->snpc, 4);
  return decode_exec(s);
}
```

RV32 当前按 4 字节取指。`inst_fetch()` 读出指令并推进 `snpc`，`decode_exec()` 决定
是否改写 `dnpc`。

PC 更新：

```c
static void exec_once(Decode *s, vaddr_t pc) {
  s->pc = pc;
  s->snpc = pc;
  isa_exec_once(s);
  cpu.pc = s->dnpc;
}
```

执行层只信任 `dnpc`。普通指令默认 `dnpc=snpc`；跳转、分支、异常会改写 `dnpc`。

异常入口：

```c
word_t isa_raise_intr(word_t NO, vaddr_t epc) {
  cpu.mepc = epc;
  cpu.mcause = NO;
  return cpu.mtvec;
}
```

`ecall` 只负责调用 `isa_raise_intr()` 并把返回值放入 `dnpc`，异常状态保存集中在
`isa_raise_intr()` 中。

## 改动代码详解

### `decode_operand()`：把指令格式统一成操作数

```c
static void decode_operand(Decode *s, int *rd, word_t *src1,
    word_t *src2, word_t *imm, int type) {
  uint32_t i = s->isa.inst;
  int rs1 = BITS(i, 19, 15);
  int rs2 = BITS(i, 24, 20);
  *rd     = BITS(i, 11, 7);
  switch (type) {
    case TYPE_I: src1R();          immI(); break;
    case TYPE_U:                   immU(); break;
    case TYPE_S: src1R(); src2R(); immS(); break;
    case TYPE_R: src1R(); src2R();         break;
    case TYPE_B: src1R(); src2R();         break;
    case TYPE_N: break;
  }
}
```

这段代码的作用是把 RISC-V 不同格式的指令先规整成 `rd/src1/src2/imm`。后面的
`INSTPAT` 只关心语义，不需要重复解析 bit field。

- I 型需要 `rs1 + imm`，用于 `addi/lw/jalr/csrr*`。
- S 型需要 `rs1 + rs2 + imm`，用于 store。
- R 型需要两个源寄存器。
- U 型只需要立即数。
- B 型需要两个源寄存器，分支立即数因为分散在多个 bit 中，在分支语义中单独拼接。

这里的设计减少了重复代码，但有一个边界：B/J 立即数较特殊，当前代码在对应
`INSTPAT` 中现场拼接，后续若要增强可读性，可以补 `immB/immJ`。

### `INSTPAT`：把匹配和执行语义放在一起

```c
INSTPAT("??????? ????? ????? 000 ????? 00100 11", addi,
    I, R(rd) = src1 + imm);
INSTPAT("??????? ????? ????? 010 ????? 00000 11", lw,
    I, R(rd) = Mr(src1 + imm, 4));
INSTPAT("??????? ????? ????? 010 ????? 01000 11", sw,
    S, Mw(src1 + imm, 4, src2));
```

这三条展示了 NEMU 的解释器风格：

- 第一个字符串是二进制匹配模板。
- 第三个参数是指令格式，决定 `decode_operand()` 怎么取操作数。
- 最后是该指令的架构语义。

例如 `lw` 的执行语义就是从 `src1 + imm` 读 4 字节写入 `rd`；`sw` 是把 `src2`
写到 `src1 + imm`。这和手册伪代码一一对应，便于对照调试。

### 跳转类指令：不要用错误的返回地址

```c
INSTPAT("??????? ????? ????? ??? ????? 11011 11", jal, N, {
  uint32_t i = INSTPAT_INST(s);
  word_t off = 0;
  off |= ((word_t)BITS(i, 31, 31) << 20);
  off |= ((word_t)BITS(i, 19, 12) << 12);
  off |= ((word_t)BITS(i, 20, 20) << 11);
  off |= ((word_t)BITS(i, 30, 21) << 1);
  off = SEXT(off, 21);
  R(rd) = s->pc + 4;
  s->dnpc = s->pc + off;
});
```

`jal` 的立即数在编码中不是连续字段，所以这里手动拼接。`R(rd) = s->pc + 4`
是关键点：返回地址是当前指令下一条，而不是跳转目标，也不是修改后的 `dnpc`。
之前调函数返回异常时，优先检查的就是这个返回地址。

```c
INSTPAT("??????? ????? ????? 000 ????? 11001 11", jalr, I, {
  word_t t = s->pc + 4;
  s->dnpc = (src1 + imm) & ~1u;
  if (rd != 0) R(rd) = t;
});
```

`jalr` 目标地址最低位要清零，这是 RISC-V 规范要求；写 `rd != 0` 是避免显式写
`x0`，虽然最后还有 `R(0)=0` 兜底，但这里提前规避更符合语义。

### RV32M：边界行为不能按 C 默认除法偷懒

```c
INSTPAT("0000001 ????? ????? 100 ????? 01100 11", div, R, {
  sword_t a = (sword_t)src1;
  sword_t b = (sword_t)src2;
  if (b == 0) {
    R(rd) = (word_t)-1;
  } else if (a == (sword_t)0x80000000 && b == -1) {
    R(rd) = (word_t)0x80000000;
  } else {
    R(rd) = (word_t)(a / b);
  }
});
```

C 语言中有符号除法溢出可能是未定义行为，RISC-V 规范则明确规定了结果。因此这里
先处理除零和 `INT_MIN / -1`，再执行普通除法。`rem/remu/divu` 也按同样思路处理，
这是 `div` 类测试能过的关键。

### `R(0)=0`：最后一道架构约束

```c
R(0) = 0;
```

这行放在 `decode_exec()` 末尾，保证任何指令即使错误写了 `x0`，最终也会恢复为 0。
它是简单、鲁棒的兜底策略；后续硬件 NPC 里则通常在寄存器堆写回阶段禁止写 `rd=0`。

## 运行方式

```sh
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
make ARCH=riscv32-nemu ALL=dummy run-batch
```

预期：

```text
nemu: HIT GOOD TRAP at pc = 0x80000030
[         dummy] PASS
```

## Debug 心得

- 如果跑到 `riscv32-nemu-interpreter-so` 并段错误，说明 NEMU 配置停在 `TARGET_SHARE`，
  需要切回 native ELF。
- 如果跳转错，优先看 `dnpc` 是否被错误改写。
- 如果函数返回错，优先检查 `jal/jalr` 写回返回地址是否用 `pc + 4`。
- 如果除法测试错，检查除零和 `INT_MIN / -1` 两个边界。
