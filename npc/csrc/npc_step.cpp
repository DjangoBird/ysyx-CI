#include "npc_step.h"

#include <cstdio>

#include <nvboard.h>
#include <verilated.h>

#include "npc_memory.h"
#include "npc_runtime.h"

void update_mem_inputs() {
  dut.imem_rdata = pmem_read32(dut.imem_addr);
  dut.dmem_rdata = pmem_read32(dut.dmem_addr);
}

bool trap_hit() {
  return dut.trap;
}

bool single_cycle() {
  update_mem_inputs();
  dut.clk = 0;
  dut.eval();
  if (trap_hit()) return true;

  update_mem_inputs();
  dut.clk = 1;
  dut.eval();
  if (dut.dmem_valid && dut.dmem_we) {
    pmem_write32(dut.dmem_addr, dut.dmem_wdata, dut.dmem_wmask);
  }

  return trap_hit();
}

void reset(int n) {
  dut.rst = 1;
  while (n-- > 0) {
    (void)single_cycle();
  }
  dut.rst = 0;
}

int run_simulation() {
  reset(10);

  while (!Verilated::gotFinish()) {
    if (gui_enabled) {
      nvboard_update();
    }
    if (single_cycle()) {
      break;
    }
  }

  if (gui_enabled) {
    nvboard_quit();
  }

  if (dut.trap) {
    if (dut.trap_code == 0) {
      std::printf("HIT GOOD TRAP at pc = 0x%08x\n", dut.imem_addr);
      return 0;
    }
    std::printf("HIT BAD TRAP at pc = 0x%08x, code = %u\n", dut.imem_addr, dut.trap_code);
    return 1;
  }

  std::printf("Simulation ended without trap\n");
  return 2;
}
