#include "npc_step.h"

#include <cstdio>

#ifdef NPC_ENABLE_NVBOARD
#include <nvboard.h>
#endif
#include <verilated.h>

#include "npc_memory.h"
#include "npc_difftest.h"
#include "npc_runtime.h"
#include "npc_trace.h"

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
  if (!dut.rst && trap_hit()) {
    uint32_t trace_pc = 0;
    uint32_t trace_instr = 0;
    npc_trace_get_current(&trace_pc, &trace_instr);
    npc_trace_commit(trace_pc, trace_instr, dut.imem_addr,
                     false, false, 0, 0, 0, 0);
    return true;
  }

  uint32_t trace_pc = 0;
  uint32_t trace_instr = 0;
  npc_trace_get_current(&trace_pc, &trace_instr);
  bool trace_mem_valid = dut.dmem_valid;
  bool trace_mem_we = dut.dmem_we;
  uint8_t trace_mem_wmask = dut.dmem_wmask;
  uint32_t trace_mem_addr = dut.dmem_addr;
  uint32_t trace_mem_wdata = dut.dmem_wdata;
  uint32_t trace_mem_rdata = dut.dmem_rdata;
  bool trace_skip = dut.rst;

  update_mem_inputs();
  dut.clk = 1;
  dut.eval();
  if (trace_mem_valid && trace_mem_we) {
    pmem_write32(trace_mem_addr, trace_mem_wdata, trace_mem_wmask);
  }

  if (!trace_skip) {
    npc_trace_commit(trace_pc, trace_instr, dut.imem_addr,
                     trace_mem_valid, trace_mem_we, trace_mem_wmask,
                     trace_mem_addr, trace_mem_wdata, trace_mem_rdata);
    npc_difftest_step(trace_pc, dut.imem_addr);
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
#ifdef NPC_ENABLE_NVBOARD
    if (gui_enabled) {
      nvboard_update();
    }
#endif
    if (single_cycle()) {
      break;
    }
  }

#ifdef NPC_ENABLE_NVBOARD
  if (gui_enabled) {
    nvboard_quit();
  }
#endif

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
