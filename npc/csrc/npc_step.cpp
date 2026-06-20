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

static uint32_t imem_response = 0;

void update_mem_inputs() {
  dut.imem_rdata = imem_response;
  dut.dmem_rdata = pmem_read32(dut.dmem_addr);
}

bool trap_hit() {
  return dut.trap;
}

bool single_cycle() {
  update_mem_inputs();
  dut.clk = 0;
  dut.eval();
  dut.dmem_rdata = pmem_read32(dut.dmem_addr);
  dut.eval();

  bool commit_valid = dut.commit_valid;
  uint32_t commit_pc = dut.commit_pc;
  uint32_t commit_instr = dut.commit_instr;
  uint32_t commit_next_pc = dut.commit_next_pc;
  bool commit_trap = dut.commit_trap;
  uint32_t imem_request_addr = dut.imem_addr;
  bool trace_mem_valid = dut.dmem_valid;
  bool trace_mem_we = dut.dmem_we;
  uint8_t trace_mem_wmask = dut.dmem_wmask;
  uint32_t trace_mem_addr = dut.dmem_addr;
  uint32_t trace_mem_wdata = dut.dmem_wdata;
  uint32_t trace_mem_rdata = dut.dmem_rdata;
  bool trace_skip = dut.rst || !commit_valid;

  dut.clk = 1;
  dut.eval();
  imem_response = pmem_read32(imem_request_addr);
  if (trace_mem_valid && trace_mem_we) {
    pmem_write32(trace_mem_addr, trace_mem_wdata, trace_mem_wmask);
  }

  if (!trace_skip) {
    npc_trace_commit(commit_pc, commit_instr, commit_next_pc,
                     trace_mem_valid, trace_mem_we, trace_mem_wmask,
                     trace_mem_addr, trace_mem_wdata, trace_mem_rdata);
    if (!commit_trap) {
      if (trace_mem_valid && !in_pmem(trace_mem_addr)) {
        npc_difftest_skip_ref(commit_next_pc);
      } else {
        npc_difftest_step(commit_pc, commit_next_pc);
      }
    }
  }

  return commit_trap || trap_hit();
}

void reset(int n) {
  imem_response = 0;
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
