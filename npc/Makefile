# ========== 基本配置 ==========
TOPNAME = top
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj_dir
BIN = $(BUILD_DIR)/$(TOPNAME)

VERILATOR = verilator

# ========== 路径 ==========
NXDC_FILES = top.nxdc

# 自动生成绑定文件
SRC_AUTO_BIND = $(abspath $(BUILD_DIR)/auto_bind.cpp)

# ========== 源文件 ==========
VSRCS = $(shell find $(abspath ./vsrc) -name "*.v")
CSRCS = $(shell find $(abspath ./csrc) -name "*.c" -o -name "*.cc" -o -name "*.cpp")
CSRCS += $(SRC_AUTO_BIND)

# ========== NVBoard ==========
INC_PATH ?=
include $(NVBOARD_HOME)/scripts/nvboard.mk

# 自动生成 auto_bind.cpp
$(SRC_AUTO_BIND): $(NXDC_FILES)
	@mkdir -p $(BUILD_DIR)
	python3 $(NVBOARD_HOME)/scripts/auto_pin_bind.py $^ $@

# ========== 编译参数 ==========
INCFLAGS = $(addprefix -I, $(INC_PATH))
CXXFLAGS += $(INCFLAGS)

VERILATOR_FLAGS += -Wall --trace -cc --build \
				   -O3 --x-assign fast --x-initial fast --noassert

# ========== 构建 ==========
all: $(BIN)

$(BIN): $(VSRCS) $(CSRCS) $(NVBOARD_ARCHIVE)
	@rm -rf $(OBJ_DIR)
	$(VERILATOR) $(VERILATOR_FLAGS) \
		--top-module $(TOPNAME) $^ \
		$(addprefix -CFLAGS , $(CXXFLAGS)) \
		$(addprefix -LDFLAGS , $(LDFLAGS)) \
		--Mdir $(OBJ_DIR) --exe -o $(abspath $(BIN))

# ========== 运行 ==========
run: $(BIN)
	@$^ $(IMG)

sim: run
	$(call git_commit, "sim RTL")

# ========== NVBoard ==========
nvboard:
	$(MAKE) -C $(NVBOARD_HOME) nvboard-archive

# ========== 清理 ==========
clean:
	rm -rf $(BUILD_DIR) wave.vcd

lint:
	verilator --lint-only -Wall -Iinclude --top-module top vsrc/*.v

.PHONY: all run sim clean nvboard

include ../Makefile
