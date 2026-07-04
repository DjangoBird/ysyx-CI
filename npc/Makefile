# ========== 基本配置 ==========
SHELL := /bin/bash
TOPNAME = top
STUID ?= ysyx_26060168
STUID_NUM = $(patsubst ysyx_%,%,$(STUID))
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj_dir
BIN = $(BUILD_DIR)/$(TOPNAME)
CI_VERILOG = $(BUILD_DIR)/$(STUID).v
IVERILOG ?= iverilog
VVP ?= vvp

VERILATOR = verilator
NPC_HOME ?= $(abspath .)
NEMU_HOME ?= $(abspath ../nemu)
AM_HOME ?= $(abspath ../abstract-machine)
export NPC_HOME NEMU_HOME AM_HOME
CROSS_COMPILE ?= riscv64-unknown-elf-

YOSYS ?= yosys
YOSYS_STA_HOME ?= $(HOME)/tools/yosys-sta
STA_FREQ_MHZ ?= 375
STA_RESULT_DIR = $(BUILD_DIR)/sta/top-$(STA_FREQ_MHZ)MHz
STA_NETLIST = $(STA_RESULT_DIR)/top.netlist.v
STA_LIBERTY = $(YOSYS_STA_HOME)/pdk/nangate45/lib/Nangate45_typ.lib
ACCESS_FAULT_ELF = $(BUILD_DIR)/access-fault.elf
ACCESS_FAULT_BIN = $(BUILD_DIR)/access-fault.bin
IVERILOG_HEX = $(BUILD_DIR)/image.hex
IVERILOG_SIM = $(BUILD_DIR)/sim-iverilog.vvp
IVERILOG_NETLIST_SIM = $(BUILD_DIR)/sim-iverilog-netlist.vvp

# ========== 路径 ==========
NXDC_FILES = top.nxdc

# ========== 源文件 ==========
VSRCS = $(shell find $(abspath ./vsrc) -name "*.v" -o -name "*.sv")
VHDRS = $(shell find $(abspath ./vsrc) -name "*.vh")
CSRCS = $(shell find $(abspath ./csrc) -name "*.c" -o -name "*.cc" -o -name "*.cpp")


# ========== Trace / Capstone ==========
CAPSTONE_HOME ?= $(abspath ../nemu/tools/capstone/repo)
LIBCAPSTONE = $(CAPSTONE_HOME)/libcapstone.so.5
CXXFLAGS += -I$(CAPSTONE_HOME)/include
LDFLAGS += $(LIBCAPSTONE) -Wl,-rpath,$(CAPSTONE_HOME) -ldl

# ========== DiffTest ==========
NPC_DIFF ?= 0
DIFF_REF_SO ?= $(NEMU_HOME)/build/riscv32-nemu-interpreter-so
DIFF_REF_CFG ?= riscv32e-ref_defconfig
ifeq ($(NPC_DIFF),1)
override ARGS += -d $(DIFF_REF_SO)
DIFF_REF_DEP = $(DIFF_REF_SO)
endif

# ========== NVBoard ==========
NPC_GUI ?= 0
INC_PATH ?=

ifeq ($(NPC_GUI),1)
  ifndef NVBOARD_HOME
    $(error NPC_GUI=1 requires NVBOARD_HOME to point to your NVBoard directory)
  endif

  include $(NVBOARD_HOME)/scripts/nvboard.mk

  # 自动生成绑定文件
  SRC_AUTO_BIND = $(abspath $(BUILD_DIR)/auto_bind.cpp)
  CSRCS += $(SRC_AUTO_BIND)
  CXXFLAGS += -DNPC_ENABLE_NVBOARD
else
  NVBOARD_ARCHIVE :=
endif

# 自动生成 auto_bind.cpp
$(SRC_AUTO_BIND): $(NXDC_FILES)
	@mkdir -p $(BUILD_DIR)
	python3 $(NVBOARD_HOME)/scripts/auto_pin_bind.py $^ $@

# ========== 编译参数 ==========
INCFLAGS = $(addprefix -I, $(INC_PATH))
CXXFLAGS += $(INCFLAGS)

VERILATOR_FLAGS += -Wall -cc --build \
                   -I$(abspath ./vsrc) \
				   -O3 --x-assign fast --x-initial fast --noassert \
				   -Wno-DECLFILENAME

$(LIBCAPSTONE):
	$(MAKE) -C $(CAPSTONE_HOME)/..

$(DIFF_REF_SO):
	$(MAKE) -C $(NEMU_HOME) $(DIFF_REF_CFG)
	$(MAKE) -C $(NEMU_HOME)


# ========== 构建 ==========
all: $(BIN)

$(BIN): $(VSRCS) $(CSRCS) $(NVBOARD_ARCHIVE) $(LIBCAPSTONE)
	@rm -rf $(OBJ_DIR)
	@mkdir -p $(BUILD_DIR)
	$(VERILATOR) $(VERILATOR_FLAGS) \
		--top-module $(TOPNAME) $(VSRCS) $(CSRCS) \
		$(addprefix -CFLAGS , $(CXXFLAGS)) \
		$(addprefix -LDFLAGS , $(LDFLAGS)) \
		--Mdir $(OBJ_DIR) --exe -o $(abspath $(BIN))

# ========== 运行 ==========
NPC_EXEC := $(BIN) $(ARGS) $(IMG)

run-env: $(BIN) $(DIFF_REF_DEP)

run: run-env
	@NPC_GUI=$(NPC_GUI) $(NPC_EXEC)

sim: run
	$(call git_commit, "sim RTL")

# ========== NVBoard ==========
nvboard:
ifndef NVBOARD_HOME
	$(error NVBOARD_HOME is not set)
endif
	$(MAKE) -C $(NVBOARD_HOME) nvboard-archive

# ========== 清理 ==========
clean:
	rm -rf $(BUILD_DIR) wave.vcd

lint: $(VSRCS)
	$(VERILATOR) --lint-only -Wall -Wno-DECLFILENAME -I$(abspath ./vsrc) --top-module $(TOPNAME) $(VSRCS)

sta: $(STA_NETLIST)
	set -o pipefail; \
	CLK_FREQ_MHZ=$(STA_FREQ_MHZ) CLK_PORT_NAME=clk \
	  $(YOSYS_STA_HOME)/bin/iEDA -script \
	  $(YOSYS_STA_HOME)/scripts/sta.tcl \
	  $(abspath $(YOSYS_STA_HOME)/scripts/default.sdc) \
	  $(abspath $(STA_NETLIST)) $(TOPNAME) nangate45 \
	  2>&1 | tee $(STA_RESULT_DIR)/sta.log

$(STA_NETLIST): $(VSRCS) scripts/synth.tcl
	@mkdir -p $(STA_RESULT_DIR)
	echo tcl $(abspath scripts/synth.tcl) $(TOPNAME) \
	  $(abspath $(STA_LIBERTY)) \
	  $$((1000000 / $(STA_FREQ_MHZ))) \
	  $(abspath $(STA_NETLIST)) \"$(VSRCS)\" | \
	  $(YOSYS) -g -l $(STA_RESULT_DIR)/yosys.log -s -

verilog: $(CI_VERILOG)

$(CI_VERILOG): $(VSRCS) $(VHDRS)
	@mkdir -p $(BUILD_DIR)
	@(printf '`ifndef NPC_RESET_PC\n`define NPC_RESET_PC 32'\''h3000_0000\n`endif\n\n'; \
	  for file in $(VHDRS); do \
	    cat $$file; \
	    printf '\n'; \
	  done; \
	  for file in $(VSRCS); do \
	    perl -ne 'next if /^\s*`include\s+"minirv_defs\.vh"\s*$$/; s/\bmodule\s+top\b/module $(STUID)/; s/32'\''h8000_0000/`NPC_RESET_PC/g; print' $$file; \
	    printf '\n'; \
	  done) > $@

$(IVERILOG_HEX): $(IMG) sim/bin2vmem.py
	@test -n "$(IMG)" || (echo "IMG is required" >&2; exit 1)
	python3 sim/bin2vmem.py $(IMG) $@

$(IVERILOG_SIM): $(CI_VERILOG) sim/iverilog_tb.v
	$(IVERILOG) -g2012 -DNPC_RESET_PC="32'h8000_0000" -o $@ sim/iverilog_tb.v $(CI_VERILOG)

sim-iverilog: $(IVERILOG_HEX) $(IVERILOG_SIM)
	$(VVP) $(IVERILOG_SIM) +MEM=$(IVERILOG_HEX)

$(IVERILOG_NETLIST_SIM): sim/iverilog_tb.v
	@test -n "$(NETLIST)" || (echo "NETLIST is required" >&2; exit 1)
	@test -n "$(CELLS)" || (echo "CELLS is required" >&2; exit 1)
	$(IVERILOG) -g2012 -DNPC_RESET_PC="32'h8000_0000" -o $@ sim/iverilog_tb.v $(NETLIST) $(CELLS)

sim-iverilog-netlist: $(IVERILOG_HEX) $(IVERILOG_NETLIST_SIM)
	$(VVP) $(IVERILOG_NETLIST_SIM) +MEM=$(IVERILOG_HEX)

test-access-fault: $(BIN) $(ACCESS_FAULT_BIN)
	$(BIN) -b $(ACCESS_FAULT_BIN)

$(ACCESS_FAULT_ELF): tests/access-fault.S tests/link.ld
	@mkdir -p $(BUILD_DIR)
	$(CROSS_COMPILE)gcc -march=rv32e_zicsr -mabi=ilp32e -nostdlib \
	  -Wl,-T,$(abspath tests/link.ld) -o $@ $<

$(ACCESS_FAULT_BIN): $(ACCESS_FAULT_ELF)
	$(CROSS_COMPILE)objcopy -O binary $< $@

.PHONY: all run run-env sim clean nvboard lint sta verilog sim-iverilog sim-iverilog-netlist test-access-fault

include ../Makefile
