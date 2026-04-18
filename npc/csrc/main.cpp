#include <verilated.h>
#include "Vtop.h"
#include <nvboard.h>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>

static Vtop dut;
static bool sim_end = false;
static uint32_t trap_pc = 0;
static uint32_t trap_code = 0;
static bool trace_en = false;
static uint64_t cycle_cnt = 0;
static uint32_t trace_pc_lo = 0;
static uint32_t trace_pc_hi = 0;

static const uint32_t PMEM_SIZE = 128u * 1024u * 1024u;
static const uint32_t PMEM_BASE = 0x80000000u;
static uint8_t pmem[PMEM_SIZE];

// 在 auto_bind.cpp 中定义
void nvboard_bind_all_pins(Vtop *top);

extern "C" void npc_ebreak(int pc, int code) {
  trap_pc = static_cast<uint32_t>(pc);
  trap_code = static_cast<uint32_t>(code);
  sim_end = true;
}

static inline bool in_pmem(uint32_t addr) {
  return addr >= PMEM_BASE && addr < PMEM_BASE + PMEM_SIZE;
}

static inline uint32_t pmem_off(uint32_t addr) {
  return addr - PMEM_BASE;
}

static uint32_t pmem_read32(uint32_t addr) {
  if (!in_pmem(addr) || !in_pmem(addr + 3)) return 0;
  uint32_t off = pmem_off(addr);
  return (uint32_t)pmem[off] |
         ((uint32_t)pmem[off + 1] << 8) |
         ((uint32_t)pmem[off + 2] << 16) |
         ((uint32_t)pmem[off + 3] << 24);
}

static void pmem_write32(uint32_t addr, uint32_t data, uint8_t wmask) {
  if (!in_pmem(addr) || !in_pmem(addr + 3)) return;
  uint32_t off = pmem_off(addr);
  if (wmask & 0x1) pmem[off] = data & 0xff;
  if (wmask & 0x2) pmem[off + 1] = (data >> 8) & 0xff;
  if (wmask & 0x4) pmem[off + 2] = (data >> 16) & 0xff;
  if (wmask & 0x8) pmem[off + 3] = (data >> 24) & 0xff;
}

static long load_img(const char *img_file) {
  if (img_file == nullptr || std::strlen(img_file) == 0) {
    std::printf("No image is given, start from empty memory.\n");
    return 0;
  }

  FILE *fp = std::fopen(img_file, "rb");
  if (fp == nullptr) {
    std::perror("Failed to open image");
    std::exit(1);
  }

  std::fseek(fp, 0, SEEK_END);
  long size = std::ftell(fp);
  std::fseek(fp, 0, SEEK_SET);

  if (size < 0 || (uint64_t)size > PMEM_SIZE) {
    std::fprintf(stderr, "Image is too large: %ld bytes\n", size);
    std::fclose(fp);
    std::exit(1);
  }

  size_t nread = std::fread(pmem, 1, (size_t)size, fp);
  std::fclose(fp);
  if (nread != (size_t)size) {
    std::fprintf(stderr, "Failed to read image: expected %ld, got %zu\n", size, nread);
    std::exit(1);
  }

  std::printf("Loaded image: %s, size = %ld bytes\n", img_file, size);
  return size;
}

static inline void update_mem_inputs() {
  dut.imem_rdata = pmem_read32(dut.imem_addr);
  dut.dmem_rdata = pmem_read32(dut.dmem_addr);
}

static inline bool trap_hit() {
  return sim_end || dut.trap;
}

// 单个时钟周期：clk 0->1，各 eval 一次
static bool single_cycle() {
  update_mem_inputs();
  dut.clk = 0;
  dut.eval();
  if (trap_hit()) return true;

  update_mem_inputs();
  bool trace_this_cycle = trace_en && (
      cycle_cnt < 40 ||
      (dut.imem_addr >= trace_pc_lo && dut.imem_addr <= trace_pc_hi)
  );
  if (trace_this_cycle) {
    std::printf("[pre  %04llu] pc=0x%08x instr=0x%08x x1=0x%08x dmem_addr=0x%08x dmem_we=%u\n",
                (unsigned long long)cycle_cnt,
                dut.imem_addr,
                dut.imem_rdata,
                dut.dbg_x1_o,
                dut.dmem_addr,
                (unsigned)dut.dmem_we);
  }
  dut.clk = 1;
  dut.eval();
  if (trap_hit()) return true;

  if (dut.dmem_valid && dut.dmem_we) {
    pmem_write32(dut.dmem_addr, dut.dmem_wdata, dut.dmem_wmask);
  }

  if (trace_this_cycle) {
    std::printf("[cycle %04llu] pc=0x%08x instr=0x%08x x1=0x%08x dmem_addr=0x%08x dmem_we=%u\n",
                (unsigned long long)cycle_cnt,
                dut.imem_addr,
                dut.imem_rdata,
                dut.dbg_x1_o,
                dut.dmem_addr,
                (unsigned)dut.dmem_we);
  }
  cycle_cnt++;

  return false;
}

// 复位 n 个周期
static void reset(int n) {
  dut.rst = 1;
  while (n-- > 0) {
    (void)single_cycle();
  }
  dut.rst = 0;
}

int main(int argc, char **argv) {
  Verilated::commandArgs(argc, argv);

  int sim_rc = 0;
  bool saw_trap = false;

  bool enable_gui = false;
  const char *gui_env = std::getenv("NPC_GUI");
  if (gui_env != nullptr && std::strcmp(gui_env, "1") == 0) {
    enable_gui = true;
  }
  const char *trace_env = std::getenv("NPC_TRACE");
  if (trace_env != nullptr && std::strcmp(trace_env, "1") == 0) {
    trace_en = true;
  }
  const char *trace_pc_env = std::getenv("NPC_TRACE_PC");
  if (trace_pc_env != nullptr) {
    unsigned lo = 0, hi = 0;
    if (std::sscanf(trace_pc_env, "%x-%x", &lo, &hi) == 2) {
      trace_pc_lo = lo;
      trace_pc_hi = hi;
    }
  }

  std::memset(pmem, 0, sizeof(pmem));
  if (argc >= 2) {
    load_img(argv[1]);
  } else {
    load_img(nullptr);
  }

  if (enable_gui) {
    nvboard_bind_all_pins(&dut);
    nvboard_init();
  }

  // 上电复位 10 个周期
  reset(10);

  // 仿真主循环：每个周期更新 NVBoard + 跑一拍
  while (!Verilated::gotFinish()) {
    if (enable_gui) {
      nvboard_update();
    }
    bool trapped = single_cycle();

    if (trapped || trap_hit()) {
      uint32_t code = dut.trap ? dut.trap_code : trap_code;
      uint32_t pc = dut.trap ? dut.imem_addr : trap_pc;
      saw_trap = true;
      if (code == 0) {
        std::printf("HIT GOOD TRAP at pc = 0x%08x\n", pc);
        sim_rc = 0;
      } else {
        std::printf("HIT BAD TRAP at pc = 0x%08x, code = %u\n", pc, code);
        sim_rc = 1;
      }
      break;
    }
  }

  if (!saw_trap) {
    std::printf("Simulation ended without trap\n");
    sim_rc = 2;
  }

  if (enable_gui) {
    nvboard_quit();
  }
  return sim_rc;
}