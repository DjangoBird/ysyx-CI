CROSS_COMPILE ?= riscv64-unknown-elf-
COMMON_CFLAGS := -fno-pic -march=rv64g -mcmodel=medany -mstrict-align
CFLAGS        += $(COMMON_CFLAGS) -static
ASFLAGS       += $(COMMON_CFLAGS) -O0
LDFLAGS       += -melf64lriscv

PICOLIBC_INCLUDE := $(HOME)/.local/riscv-picolibc/usr/lib/picolibc/riscv64-unknown-elf/include
ifneq ($(wildcard $(PICOLIBC_INCLUDE)/stdio.h),)
CFLAGS += -isystem $(PICOLIBC_INCLUDE)
endif

# overwrite ARCH_H defined in $(AM_HOME)/Makefile
ARCH_H := arch/riscv.h
