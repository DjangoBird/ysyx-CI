#include "npc_step.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef NPC_ENABLE_NVBOARD
#include <nvboard.h>
#endif
#include <verilated.h>

#include "npc_memory.h"
#include "npc_difftest.h"
#include "npc_runtime.h"
#include "npc_trace.h"

enum class BusMode {
  ValidOnly,
  FullHandshake,
};

struct MemoryBus {
  bool pending = false;
  int delay = 0;
  bool response_valid = false;
  uint32_t response_data = 0;
  uint32_t addr = 0;
  uint32_t wdata = 0;
  uint8_t wmask = 0;
  bool write = false;
};

struct RequestMonitor {
  bool stalled = false;
  uint32_t addr = 0;
  uint32_t wdata = 0;
  uint8_t wmask = 0;
  bool write = false;
};

static BusMode bus_mode = BusMode::ValidOnly;
static MemoryBus imem_bus;
static MemoryBus dmem_bus;
static RequestMonitor imem_monitor;
static RequestMonitor dmem_monitor;
static uint32_t random_state = 1;
static bool bus_configured = false;
static bool imem_ready_signal = false;
static bool dmem_ready_signal = false;
static bool commit_ready_signal = false;
static uint64_t imem_request_stalls = 0;
static uint64_t imem_response_stalls = 0;
static uint64_t dmem_request_stalls = 0;
static uint64_t dmem_response_stalls = 0;

static uint32_t next_random() {
  random_state ^= random_state << 13;
  random_state ^= random_state >> 17;
  random_state ^= random_state << 5;
  return random_state;
}

static bool random_ready() {
  return bus_mode == BusMode::ValidOnly || (next_random() & 3u) != 0;
}

static int response_delay() {
  return bus_mode == BusMode::ValidOnly ? 0 : (int)(next_random() & 3u);
}

static void configure_bus() {
  if (bus_configured) return;
  const char *mode = std::getenv("NPC_BUS_MODE");
  if (mode != nullptr &&
      (std::strcmp(mode, "full") == 0 || std::strcmp(mode, "random") == 0)) {
    bus_mode = BusMode::FullHandshake;
  }
  const char *seed = std::getenv("NPC_BUS_SEED");
  if (seed != nullptr) {
    random_state = (uint32_t)std::strtoul(seed, nullptr, 0);
    if (random_state == 0) random_state = 1;
  }
  bus_configured = true;
}

static void prepare_response(MemoryBus *bus, bool instruction) {
  if (!bus->pending || bus->response_valid) return;
  if (bus->delay > 0) {
    --bus->delay;
    return;
  }

  if (instruction || !bus->write) {
    bus->response_data = pmem_read32(bus->addr);
  } else {
    pmem_write32(bus->addr, bus->wdata, bus->wmask);
    bus->response_data = 0;
  }
  bus->pending = false;
  bus->response_valid = true;
}

void update_mem_inputs() {
  dut.imem_req_ready = imem_ready_signal;
  dut.imem_resp_valid = imem_bus.response_valid;
  dut.imem_rdata = imem_bus.response_data;

  dut.dmem_req_ready = dmem_ready_signal;
  dut.dmem_resp_valid = dmem_bus.response_valid;
  dut.dmem_rdata = dmem_bus.response_data;
  dut.commit_ready = commit_ready_signal;
}

static void begin_bus_cycle() {
  configure_bus();
  if (dut.rst) {
    imem_ready_signal = false;
    dmem_ready_signal = false;
    commit_ready_signal = false;
    imem_bus.response_valid = false;
    dmem_bus.response_valid = false;
    update_mem_inputs();
    return;
  }
  prepare_response(&imem_bus, true);
  prepare_response(&dmem_bus, false);

  imem_ready_signal =
      !imem_bus.pending && !imem_bus.response_valid && random_ready();
  dmem_ready_signal =
      !dmem_bus.pending && !dmem_bus.response_valid && random_ready();
  commit_ready_signal = random_ready();
  update_mem_inputs();
}

static void check_request_stability(RequestMonitor *monitor, const char *name,
                                    bool valid, bool ready, uint32_t addr,
                                    bool write, uint8_t wmask, uint32_t wdata) {
  if (valid && monitor->stalled &&
      (monitor->addr != addr || monitor->write != write ||
       monitor->wmask != wmask || monitor->wdata != wdata)) {
      std::fprintf(stderr, "%s request changed while stalled\n", name);
      std::exit(1);
  }
  if (valid && !ready) {
    monitor->stalled = true;
    monitor->addr = addr;
    monitor->write = write;
    monitor->wmask = wmask;
    monitor->wdata = wdata;
  } else {
    monitor->stalled = false;
  }
}

bool trap_hit() {
  return dut.trap;
}

bool single_cycle() {
  begin_bus_cycle();
  dut.clk = 0;
  dut.eval();

  if (dut.rst) {
    imem_monitor = {};
    dmem_monitor = {};
  } else {
    check_request_stability(&imem_monitor, "instruction bus",
                            dut.imem_req_valid, dut.imem_req_ready,
                            dut.imem_addr, false, 0, 0);
    check_request_stability(&dmem_monitor, "data bus",
                            dut.dmem_valid, dut.dmem_req_ready,
                            dut.dmem_addr, dut.dmem_we, dut.dmem_wmask,
                            dut.dmem_wdata);
  }

  bool commit_valid = dut.commit_valid;
  uint32_t commit_pc = dut.commit_pc;
  uint32_t commit_instr = dut.commit_instr;
  uint32_t commit_next_pc = dut.commit_next_pc;
  bool commit_trap = dut.commit_trap;
  bool imem_request_fire = dut.imem_req_valid && dut.imem_req_ready;
  bool imem_response_fire = dut.imem_resp_valid && dut.imem_resp_ready;
  bool dmem_request_fire = dut.dmem_valid && dut.dmem_req_ready;
  bool dmem_response_fire = dut.dmem_resp_valid && dut.dmem_resp_ready;
  if (dut.imem_req_valid && !dut.imem_req_ready) ++imem_request_stalls;
  if (dut.imem_resp_valid && !dut.imem_resp_ready) ++imem_response_stalls;
  if (dut.dmem_valid && !dut.dmem_req_ready) ++dmem_request_stalls;
  if (dut.dmem_resp_valid && !dut.dmem_resp_ready) ++dmem_response_stalls;
  uint32_t imem_request_addr = dut.imem_addr;
  bool dmem_request_we = dut.dmem_we;
  uint8_t dmem_request_wmask = dut.dmem_wmask;
  uint32_t dmem_request_addr = dut.dmem_addr;
  uint32_t dmem_request_wdata = dut.dmem_wdata;
  bool trace_mem_valid = dut.commit_mem_valid;
  bool trace_mem_we = dut.commit_mem_we;
  uint8_t trace_mem_wmask = dut.commit_mem_wmask;
  uint32_t trace_mem_addr = dut.commit_mem_addr;
  uint32_t trace_mem_wdata = dut.commit_mem_wdata;
  uint32_t trace_mem_rdata = dut.commit_mem_rdata;
  bool trace_skip = dut.rst || !commit_valid;

  dut.clk = 1;
  dut.eval();

  if (imem_response_fire) imem_bus.response_valid = false;
  if (dmem_response_fire) dmem_bus.response_valid = false;

  if (imem_request_fire) {
    imem_bus.pending = true;
    imem_bus.delay = response_delay();
    imem_bus.addr = imem_request_addr;
  }
  if (dmem_request_fire) {
    dmem_bus.pending = true;
    dmem_bus.delay = response_delay();
    dmem_bus.addr = dmem_request_addr;
    dmem_bus.write = dmem_request_we;
    dmem_bus.wmask = dmem_request_wmask;
    dmem_bus.wdata = dmem_request_wdata;
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

  if (commit_trap && bus_mode == BusMode::FullHandshake) {
    std::printf(
        "SimpleBus stalls: imem-req=%llu imem-resp=%llu "
        "dmem-req=%llu dmem-resp=%llu\n",
        (unsigned long long)imem_request_stalls,
        (unsigned long long)imem_response_stalls,
        (unsigned long long)dmem_request_stalls,
        (unsigned long long)dmem_response_stalls);
  }
  return commit_trap || trap_hit();
}

void reset(int n) {
  imem_bus = {};
  dmem_bus = {};
  imem_monitor = {};
  dmem_monitor = {};
  bus_configured = false;
  imem_ready_signal = false;
  dmem_ready_signal = false;
  commit_ready_signal = false;
  imem_request_stalls = 0;
  imem_response_stalls = 0;
  dmem_request_stalls = 0;
  dmem_response_stalls = 0;
  dut.rst = 1;
  while (n-- > 0) {
    (void)single_cycle();
  }
  imem_bus = {};
  dmem_bus = {};
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
