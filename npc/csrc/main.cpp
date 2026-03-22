#include <verilated.h>
#include "Vtop.h"
#include <nvboard.h>

static Vtop dut;

// 在 auto_bind.cpp 中定义
void nvboard_bind_all_pins(Vtop *top);

// 单个时钟周期：clk 0->1，各 eval 一次
static void single_cycle() {
  dut.clk = 0; dut.eval();
  dut.clk = 1; dut.eval();
}

// 复位 n 个周期
static void reset(int n) {
  dut.rst = 1;
  while (n-- > 0) {
    single_cycle();
  }
  dut.rst = 0;
}

int main(int argc, char **argv) {
  Verilated::commandArgs(argc, argv);

  nvboard_bind_all_pins(&dut);
  nvboard_init();

  // 上电复位 10 个周期
  reset(10);

  // 仿真主循环：每个周期更新 NVBoard + 跑一拍
  while (!Verilated::gotFinish()) {
    nvboard_update();
    single_cycle();
  }

  nvboard_quit();
  return 0;
}