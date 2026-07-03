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

### 场景 1：程序启动后直接段错误

现象：

```text
make ARCH=riscv32-nemu ... run
...
riscv32-nemu-interpreter-so
Segmentation fault
```

判断：这通常不是客户程序段错误，而是 NEMU 当前被配置成 DiffTest REF shared object。
AM 的 `run` 期望执行普通 ELF 模拟器，如果 `.config` 仍是 `TARGET_SHARE`，就会把 `.so`
当可执行程序跑。

排查：

```bash
cd ~/ysyx-workbench/nemu
rg -n "CONFIG_TARGET|CONFIG_RVE|CONFIG_TRACE" .config include/generated/autoconf.h
file build/riscv32-nemu-interpreter*
```

修复：

```bash
make ISA=riscv32 menuconfig
# 选择 native ELF 目标，而不是 shared object
make ISA=riscv32
```

### 场景 2：某条指令后 PC 跑飞

工具组合：

```bash
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
make ARCH=riscv32-nemu ALL=jump run-batch
less build/jump-riscv32-nemu.txt
less build/nemu-log.txt
```

排查顺序：

1. 在 `nemu-log.txt` 找到最后正常 PC。
2. 在 `.txt` 中找到这条指令。
3. 如果是普通指令，检查 `dnpc` 是否默认等于 `snpc`。
4. 如果是 branch，检查分支条件和 B 型立即数拼接。
5. 如果是 `jal`，检查 J 型立即数和 `rd = pc + 4`。
6. 如果是 `jalr`，检查目标地址是否 `(src1 + imm) & ~1u`。

gdb 断点：

```gdb
b decode_exec
run
p/x s->pc
p/x s->snpc
p/x s->dnpc
p/x s->isa.inst.val
```

关键判断：`cpu.pc` 最终来自 `s->dnpc`。如果 `dnpc` 在错误指令前已经不对，问题在
译码/立即数/条件判断；如果 `dnpc` 正确但下一轮取指错，再看取指和内存加载。

### 场景 3：函数返回错或 ftrace 调用层级乱

优先查 `jal/jalr`：

- `jal` 写回返回地址必须是 `pc + 4`。
- `jalr` 也要先保存 `pc + 4`，再改 `dnpc`。
- `jalr` 目标最低位必须清零。
- `rd=x0` 时不要写返回地址。

验证方式：

```bash
make ARCH=riscv32-nemu ALL=dummy run-batch
less build/nemu-log.txt
```

如果 ftrace 显示 call 后 ret 到奇怪地址，先在 `.txt` 查 call 指令是否写了正确的
`ra/x1`。

### 场景 4：load/store 测试失败

排查链：

```text
指令格式 -> rs1/rs2/imm -> 地址 -> len -> 符号扩展 -> rd 写回
```

步骤：

1. 找到失败前最后一条 load/store。
2. 用 gdb 在 `paddr_read/paddr_write` 下断点。
3. 查看 `addr/len/data`。
4. 回到 `decode_exec` 查看 `src1/src2/imm/rd`。
5. 对 `lb/lbu/lh/lhu` 特别检查符号扩展和零扩展。

常用 gdb：

```gdb
b paddr_read
b paddr_write
p/x addr
p len
p/x ret
p/x cpu.gpr[10]
```

### 场景 5：RV32M 除法测试失败

不要直接相信 C 的 `/` 和 `%`。RISC-V 规定了边界行为：

- 除数为 0：`div/divu` 返回全 1，`rem/remu` 返回被除数。
- `INT_MIN / -1`：商为 `INT_MIN`，余数为 0。

排查：

```bash
make ARCH=riscv32-nemu ALL=div run-batch
```

如果只有边界用例失败，问题通常在 `div/rem` 的特殊分支，而不是乘法器或普通除法。

### 场景 6：`x0` 被写坏

现象：大量无关测试同时异常，或者 `x0` 打印不为 0。

检查：

```c
R(0) = 0;
```

这行必须在 `decode_exec()` 末尾兜底。即使某条指令错误地写了 `rd=0`，最终也要恢复。
