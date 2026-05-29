#include <verilated.h>
#include <nvboard.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "npc_memory.h"
#include "npc_runtime.h"
#include "npc_step.h"

// 在 auto_bind.cpp 中定义
void nvboard_bind_all_pins(Vtop *top);

int main(int argc, char **argv) {
  Verilated::commandArgs(argc, argv);
  const char *gui_env = std::getenv("NPC_GUI");
  if (gui_env != nullptr && std::strcmp(gui_env, "1") == 0) {
    gui_enabled = true;
  }

  std::memset(pmem, 0, sizeof(pmem));
  if (argc >= 2) {
    load_img(argv[1]);
  } else {
    load_img(nullptr);
  }

  if (gui_enabled) {
    nvboard_bind_all_pins(&dut);
    nvboard_init();
  }

  return run_simulation();
}
