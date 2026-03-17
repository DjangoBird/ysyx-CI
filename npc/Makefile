# 顶层模块名字
TOPNAME = top

# 工具
VERILATOR = verilator

# 源文件
VSRCS = $(wildcard vsrc/*.v)
CSRCS = $(wildcard csrc/*.cpp)

# 构建目录
BUILD_DIR = build

# 默认目标
all: $(BUILD_DIR)/V$(TOPNAME)

# Verilator生成C++
$(BUILD_DIR)/V$(TOPNAME): $(VSRCS) $(CSRCS)
	@mkdir -p $(BUILD_DIR)
	$(VERILATOR) -Wall --trace --cc $(VSRCS) \
		--exe $(CSRCS) \
		--build \
		-Mdir $(BUILD_DIR) \
		--top-module $(TOPNAME)

# 运行仿真
sim: 
	$(call git_commit, "sim RTL") # DO NOT REMOVE THIS LINE!!!
	all
	./$(BUILD_DIR)/V$(TOPNAME)

# 清理
clean:
	rm -rf $(BUILD_DIR) wave.vcd

include ../Makefile
