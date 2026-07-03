#include "npc_step.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef NPC_ENABLE_NVBOARD
#include <nvboard.h>
#endif
#include <verilated.h>

#include "npc_difftest.h"
#include "npc_memory.h"
#include "npc_runtime.h"
#include "npc_trace.h"

enum class AxiMode {
  Fixed,
  Random,
};

struct AxiReadSlave {
  bool pending = false;
  int delay = 0;
  uint32_t addr = 0;
  bool rvalid = false;
  uint32_t rdata = 0;
};

struct AxiWriteSlave {
  bool aw_captured = false;
  uint32_t awaddr = 0;
  bool w_captured = false;
  uint32_t wdata = 0;
  uint8_t wstrb = 0;
  bool pending = false;
  int delay = 0;
  bool bvalid = false;
};

struct RequestMonitor {
  bool stalled = false;
  uint32_t addr = 0;
  uint32_t data = 0;
  uint8_t strb = 0;
};

static AxiMode axi_mode = AxiMode::Fixed;
static AxiReadSlave mem_read;
static AxiWriteSlave mem_write;
static RequestMonitor ar_monitor;
static RequestMonitor aw_monitor;
static RequestMonitor w_monitor;
static uint32_t random_state = 1;
static bool configured = false;
static bool arready = false;
static bool awready = false;
static bool wready = false;
static bool commit_ready = false;
static uint64_t cycle_count = 0;
static uint64_t instruction_count = 0;
static uint64_t ar_stalls = 0;
static uint64_t r_stalls = 0;
static uint64_t aw_stalls = 0;
static uint64_t w_stalls = 0;
static uint64_t b_stalls = 0;
static uint64_t last_commit_cycle = 0;

static uint32_t next_random() {
  random_state ^= random_state << 13;
  random_state ^= random_state >> 17;
  random_state ^= random_state << 5;
  return random_state;
}

static bool random_ready() {
  return axi_mode == AxiMode::Fixed || (next_random() & 3u) != 0;
}

static int response_delay() {
  return axi_mode == AxiMode::Fixed ? 0 : (int)(next_random() & 3u);
}

static void configure_axi() {
  if (configured) return;
  const char *mode = std::getenv("NPC_AXI_MODE");
  if (mode == nullptr) mode = std::getenv("NPC_BUS_MODE");
  if (mode != nullptr &&
      (std::strcmp(mode, "random") == 0 ||
       std::strcmp(mode, "full") == 0)) {
    axi_mode = AxiMode::Random;
  }
  const char *seed = std::getenv("NPC_AXI_SEED");
  if (seed == nullptr) seed = std::getenv("NPC_BUS_SEED");
  if (seed != nullptr) {
    random_state = (uint32_t)std::strtoul(seed, nullptr, 0);
    if (random_state == 0) random_state = 1;
  }
  configured = true;
}

static void prepare_read_response() {
  if (!mem_read.pending || mem_read.rvalid) return;
  if (mem_read.delay > 0) {
    --mem_read.delay;
    return;
  }
  mem_read.rdata = pmem_read32(mem_read.addr);
  mem_read.pending = false;
  mem_read.rvalid = true;
}

static void prepare_write_response() {
  if (!mem_write.pending || mem_write.bvalid) return;
  if (mem_write.delay > 0) {
    --mem_write.delay;
    return;
  }
  pmem_write32(mem_write.awaddr, mem_write.wdata, mem_write.wstrb);
  mem_write.pending = false;
  mem_write.bvalid = true;
}

void update_mem_inputs() {
  dut.mem_axi_arready = arready;
  dut.mem_axi_rvalid = mem_read.rvalid;
  dut.mem_axi_rdata = mem_read.rdata;
  dut.mem_axi_rresp = 0;
  dut.mem_axi_awready = awready;
  dut.mem_axi_wready = wready;
  dut.mem_axi_bvalid = mem_write.bvalid;
  dut.mem_axi_bresp = 0;
  dut.commit_ready = commit_ready;
}

static void begin_axi_cycle() {
  configure_axi();
  if (dut.rst) {
    arready = false;
    awready = false;
    wready = false;
    commit_ready = false;
    mem_read.rvalid = false;
    mem_write.bvalid = false;
    update_mem_inputs();
    return;
  }

  prepare_read_response();
  prepare_write_response();
  arready = !mem_read.pending && !mem_read.rvalid && random_ready();
  awready = !mem_write.aw_captured && !mem_write.pending &&
            !mem_write.bvalid && random_ready();
  wready = !mem_write.w_captured && !mem_write.pending &&
           !mem_write.bvalid && random_ready();
  commit_ready = random_ready();
  update_mem_inputs();
}

static void check_stability(RequestMonitor *monitor, const char *name,
                            bool valid, bool ready, uint32_t addr,
                            uint32_t data, uint8_t strb) {
  if (valid && monitor->stalled &&
      (monitor->addr != addr || monitor->data != data ||
       monitor->strb != strb)) {
    std::fprintf(stderr, "%s changed while VALID was stalled\n", name);
    std::exit(1);
  }
  if (valid && !ready) {
    monitor->stalled = true;
    monitor->addr = addr;
    monitor->data = data;
    monitor->strb = strb;
  } else {
    monitor->stalled = false;
  }
}

static void print_statistics() {
  double ipc = cycle_count == 0
      ? 0.0
      : (double)instruction_count / (double)cycle_count;
  std::printf(
      "NPC performance: cycles=%llu instructions=%llu IPC=%.6f\n",
      (unsigned long long)cycle_count,
      (unsigned long long)instruction_count, ipc);
  if (axi_mode == AxiMode::Random) {
    std::printf(
        "AXI-Lite stalls: AR=%llu R=%llu AW=%llu W=%llu B=%llu\n",
        (unsigned long long)ar_stalls, (unsigned long long)r_stalls,
        (unsigned long long)aw_stalls, (unsigned long long)w_stalls,
        (unsigned long long)b_stalls);
  }
}

bool trap_hit() {
  return dut.trap;
}

bool single_cycle() {
  begin_axi_cycle();
  dut.clk = 0;
  dut.eval();

  if (dut.rst) {
    ar_monitor = {};
    aw_monitor = {};
    w_monitor = {};
  } else {
    check_stability(&ar_monitor, "MEM AR", dut.mem_axi_arvalid,
                    dut.mem_axi_arready, dut.mem_axi_araddr, 0, 0);
    check_stability(&aw_monitor, "MEM AW", dut.mem_axi_awvalid,
                    dut.mem_axi_awready, dut.mem_axi_awaddr, 0, 0);
    check_stability(&w_monitor, "MEM W", dut.mem_axi_wvalid,
                    dut.mem_axi_wready, 0, dut.mem_axi_wdata,
                    dut.mem_axi_wstrb);
  }

  bool ar_fire = dut.mem_axi_arvalid && dut.mem_axi_arready;
  bool r_fire = dut.mem_axi_rvalid && dut.mem_axi_rready;
  bool aw_fire = dut.mem_axi_awvalid && dut.mem_axi_awready;
  bool w_fire = dut.mem_axi_wvalid && dut.mem_axi_wready;
  bool b_fire = dut.mem_axi_bvalid && dut.mem_axi_bready;

  if (dut.mem_axi_arvalid && !dut.mem_axi_arready) ++ar_stalls;
  if (dut.mem_axi_rvalid && !dut.mem_axi_rready) ++r_stalls;
  if (dut.mem_axi_awvalid && !dut.mem_axi_awready) ++aw_stalls;
  if (dut.mem_axi_wvalid && !dut.mem_axi_wready) ++w_stalls;
  if (dut.mem_axi_bvalid && !dut.mem_axi_bready) ++b_stalls;

  bool current_commit_valid = dut.commit_valid;
  uint32_t commit_pc = dut.commit_pc;
  uint32_t commit_instr = dut.commit_instr;
  uint32_t commit_next_pc = dut.commit_next_pc;
  bool commit_trap = dut.commit_trap;
  bool trace_mem_valid = dut.commit_mem_valid;
  bool trace_mem_we = dut.commit_mem_we;
  uint8_t trace_mem_wmask = dut.commit_mem_wmask;
  uint32_t trace_mem_addr = dut.commit_mem_addr;
  uint32_t trace_mem_wdata = dut.commit_mem_wdata;
  uint32_t trace_mem_rdata = dut.commit_mem_rdata;
  uint32_t araddr = dut.mem_axi_araddr;
  uint32_t awaddr = dut.mem_axi_awaddr;
  uint32_t wdata = dut.mem_axi_wdata;
  uint8_t wstrb = dut.mem_axi_wstrb;

  dut.clk = 1;
  dut.eval();

  if (!dut.rst) ++cycle_count;
  if (r_fire) mem_read.rvalid = false;
  if (b_fire) {
    mem_write.bvalid = false;
    mem_write.aw_captured = false;
    mem_write.w_captured = false;
  }

  if (ar_fire) {
    mem_read.pending = true;
    mem_read.delay = response_delay();
    mem_read.addr = araddr;
  }
  if (aw_fire) {
    mem_write.aw_captured = true;
    mem_write.awaddr = awaddr;
  }
  if (w_fire) {
    mem_write.w_captured = true;
    mem_write.wdata = wdata;
    mem_write.wstrb = wstrb;
  }
  if (mem_write.aw_captured && mem_write.w_captured &&
      !mem_write.pending && !mem_write.bvalid) {
    mem_write.pending = true;
    mem_write.delay = response_delay();
  }

  if (!dut.rst && current_commit_valid) {
    ++instruction_count;
    last_commit_cycle = cycle_count;
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

  if (!dut.rst && std::getenv("NPC_AXI_DEBUG") != nullptr &&
      cycle_count > last_commit_cycle + 32 &&
      ((cycle_count - last_commit_cycle) % 32 == 0)) {
    std::fprintf(
        stderr,
        "AXI debug cycle=%llu pc=0x%08x AR=%u/%u@0x%08x "
        "R=%u/%u AW=%u/%u W=%u/%u B=%u/%u\n",
        (unsigned long long)cycle_count, dut.dbg_pc_o,
        dut.mem_axi_arvalid, dut.mem_axi_arready, dut.mem_axi_araddr,
        dut.mem_axi_rvalid, dut.mem_axi_rready,
        dut.mem_axi_awvalid, dut.mem_axi_awready,
        dut.mem_axi_wvalid, dut.mem_axi_wready,
        dut.mem_axi_bvalid, dut.mem_axi_bready);
  }

  if (commit_trap) print_statistics();
  return commit_trap || trap_hit();
}

void reset(int n) {
  mem_read = {};
  mem_write = {};
  ar_monitor = {};
  aw_monitor = {};
  w_monitor = {};
  configured = false;
  arready = false;
  awready = false;
  wready = false;
  commit_ready = false;
  cycle_count = 0;
  instruction_count = 0;
  last_commit_cycle = 0;
  ar_stalls = r_stalls = aw_stalls = w_stalls = b_stalls = 0;
  dut.rst = 1;
  while (n-- > 0) {
    (void)single_cycle();
  }
  mem_read = {};
  mem_write = {};
  dut.rst = 0;
}

int run_simulation() {
  reset(10);
  while (!Verilated::gotFinish()) {
#ifdef NPC_ENABLE_NVBOARD
    if (gui_enabled) nvboard_update();
#endif
    if (single_cycle()) break;
  }
#ifdef NPC_ENABLE_NVBOARD
  if (gui_enabled) nvboard_quit();
#endif
  if (dut.trap) {
    if (dut.trap_code == 0) {
      std::printf("HIT GOOD TRAP at pc = 0x%08x\n", dut.commit_pc);
      return 0;
    }
    std::printf("HIT BAD TRAP at pc = 0x%08x, code = %u\n",
                dut.commit_pc, dut.trap_code);
    return 1;
  }
  std::printf("Simulation ended without trap\n");
  return 2;
}
