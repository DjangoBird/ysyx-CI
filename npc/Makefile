# ========== 基本配置 ==========
TOPNAME = top
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj_dir
BIN = $(BUILD_DIR)/$(TOPNAME)

VERILATOR = verilator
NPC_HOME ?= $(abspath .)
NEMU_HOME ?= $(abspath ../nemu)
AM_HOME ?= $(abspath ../abstract-machine)
export NPC_HOME NEMU_HOME AM_HOME

# ========== 路径 ==========
NXDC_FILES = top.nxdc

# ========== 源文件 ==========
VSRCS = $(shell find $(abspath ./vsrc) -name "*.v" -o -name "*.sv")
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

.PHONY: all run run-env sim clean nvboard lint

include ../Makefile
