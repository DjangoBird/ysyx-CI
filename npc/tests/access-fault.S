.section .text
.globl _start

_start:
  la t0, handler
  csrw mtvec, t0

  li s0, 5
  li t1, 0x04000000
  lw t2, 0(t1)

  li s0, 7
  sw zero, 0(t1)

  li s0, 1
  la s1, after_ifault
  jr t1

after_ifault:
  li a0, 0
  ebreak

bad:
  li a0, 1
  ebreak

handler:
  csrr t0, mcause
  bne t0, s0, bad

  li t1, 1
  beq s0, t1, ifault_return

  csrr t0, mepc
  addi t0, t0, 4
  csrw mepc, t0
  mret

ifault_return:
  csrw mepc, s1
  mret
