#include "npc_difftest.h"

#include <dlfcn.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "npc_runtime.h"

enum { DIFFTEST_TO_DUT, DIFFTEST_TO_REF };

struct DiffCpuState {
  uint32_t gpr[16];
  uint32_t pc;
};

static void (*ref_difftest_memcpy)(uint32_t addr, void *buf, size_t n, bool direction) = nullptr;
static void (*ref_difftest_regcpy)(void *dut, bool direction) = nullptr;
static void (*ref_difftest_exec)(uint64_t n) = nullptr;
static void (*ref_difftest_raise_intr)(uint64_t no) = nullptr;
static bool difftest_on = false;

static uint32_t dut_reg(int idx) {
  switch (idx) {
    case 0: return dut.dbg_x0_o;
    case 1: return dut.dbg_x1_o;
    case 2: return dut.dbg_x2_o;
    case 3: return dut.dbg_x3_o;
    case 4: return dut.dbg_x4_o;
    case 5: return dut.dbg_x5_o;
    case 6: return dut.dbg_x6_o;
    case 7: return dut.dbg_x7_o;
    case 8: return dut.dbg_x8_o;
    case 9: return dut.dbg_x9_o;
    case 10: return dut.dbg_x10_o;
    case 11: return dut.dbg_x11_o;
    case 12: return dut.dbg_x12_o;
    case 13: return dut.dbg_x13_o;
    case 14: return dut.dbg_x14_o;
    case 15: return dut.dbg_x15_o;
    default: return 0;
  }
}

static DiffCpuState dut_snapshot(uint32_t pc) {
  DiffCpuState s = {};
  for (int i = 0; i < 16; ++i) {
    s.gpr[i] = dut_reg(i);
  }
  s.gpr[0] = 0;
  s.pc = pc;
  return s;
}

static void *checked_dlsym(void *handle, const char *name) {
  void *sym = dlsym(handle, name);
  if (sym == nullptr) {
    std::fprintf(stderr, "DiffTest: missing symbol %s: %s\n", name, dlerror());
    std::exit(1);
  }
  return sym;
}

bool npc_difftest_enabled() {
  return difftest_on;
}

void npc_difftest_init(const char *ref_so_file, long img_size, int port) {
  if (ref_so_file == nullptr || ref_so_file[0] == '\0') {
    return;
  }

  void *handle = dlopen(ref_so_file, RTLD_LAZY);
  if (handle == nullptr) {
    std::fprintf(stderr, "DiffTest: failed to open %s: %s\n", ref_so_file, dlerror());
    std::exit(1);
  }

  ref_difftest_memcpy = (void (*)(uint32_t, void *, size_t, bool))checked_dlsym(handle, "difftest_memcpy");
  ref_difftest_regcpy = (void (*)(void *, bool))checked_dlsym(handle, "difftest_regcpy");
  ref_difftest_exec = (void (*)(uint64_t))checked_dlsym(handle, "difftest_exec");
  ref_difftest_raise_intr = (void (*)(uint64_t))checked_dlsym(handle, "difftest_raise_intr");
  auto ref_difftest_init = (void (*)(int))checked_dlsym(handle, "difftest_init");

  std::printf("DiffTest: %s\n", ref_so_file);
  ref_difftest_init(port);
  if (img_size > 0) {
    ref_difftest_memcpy(PMEM_BASE, pmem, (size_t)img_size, DIFFTEST_TO_REF);
  }

  DiffCpuState regs = dut_snapshot(dut.imem_addr);
  ref_difftest_regcpy(&regs, DIFFTEST_TO_REF);
  difftest_on = true;
}

void npc_difftest_step(uint32_t pc, uint32_t npc) {
  if (!difftest_on) return;

  ref_difftest_exec(1);

  DiffCpuState ref = {};
  ref_difftest_regcpy(&ref, DIFFTEST_TO_DUT);
  DiffCpuState dut_state = dut_snapshot(npc);

  bool ok = true;
  for (int i = 0; i < 16; ++i) {
    if (ref.gpr[i] != dut_state.gpr[i]) {
      std::fprintf(stderr,
                   "DiffTest: register mismatch after pc=0x%08x: x%d ref=0x%08x dut=0x%08x\n",
                   pc, i, ref.gpr[i], dut_state.gpr[i]);
      ok = false;
    }
  }
  if (ref.pc != dut_state.pc) {
    std::fprintf(stderr,
                 "DiffTest: pc mismatch after pc=0x%08x: ref=0x%08x dut=0x%08x\n",
                 pc, ref.pc, dut_state.pc);
    ok = false;
  }

  if (!ok) {
    std::exit(1);
  }
}

