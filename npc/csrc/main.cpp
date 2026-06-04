#include <verilated.h>
#ifdef NPC_ENABLE_NVBOARD
#include <nvboard.h>
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "npc_difftest.h"
#include "npc_memory.h"
#include "npc_runtime.h"
#include "npc_step.h"
#include "npc_sdb.h"
#include "npc_trace.h"

#ifdef NPC_ENABLE_NVBOARD
// 在 auto_bind.cpp 中定义
void nvboard_bind_all_pins(Vtop *top);
#endif

struct CmdArgs {
  const char *img_file = nullptr;
  const char *diff_so = nullptr;
  int diff_port = 1234;
  bool batch = false;
};

static void usage(const char *prog) {
  std::printf("Usage: %s [OPTION...] IMAGE\n", prog);
  std::printf("  -b,--batch              run without interactive sdb\n");
  std::printf("  -d,--diff=REF_SO        run DiffTest with reference REF_SO\n");
  std::printf("  -p,--port=PORT          DiffTest port, kept for NEMU-compatible API\n");
  std::printf("  -l,--log=FILE           write trace log to FILE\n");
  std::printf("  --ftrace=ELF            load ELF symbols for NPC ftrace\n");
}

static bool match_opt(const char *arg, const char *short_opt, const char *long_opt) {
  return std::strcmp(arg, short_opt) == 0 || std::strcmp(arg, long_opt) == 0;
}

static const char *opt_value(int *i, int argc, char **argv, const char *name) {
  const char *eq = std::strchr(argv[*i], '=');
  if (eq != nullptr) return eq + 1;
  if (*i + 1 >= argc) {
    std::fprintf(stderr, "Missing value for %s\n", name);
    std::exit(1);
  }
  ++(*i);
  return argv[*i];
}

static CmdArgs parse_args(int argc, char **argv) {
  CmdArgs args;
  for (int i = 1; i < argc; ++i) {
    const char *arg = argv[i];
    if (match_opt(arg, "-b", "--batch")) {
      args.batch = true;
    } else if (std::strncmp(arg, "--diff", 6) == 0 || std::strcmp(arg, "-d") == 0) {
      args.diff_so = opt_value(&i, argc, argv, "--diff");
    } else if (std::strncmp(arg, "--port", 6) == 0 || std::strcmp(arg, "-p") == 0) {
      args.diff_port = std::atoi(opt_value(&i, argc, argv, "--port"));
    } else if (std::strncmp(arg, "--log", 5) == 0 || std::strcmp(arg, "-l") == 0) {
      setenv("NPC_TRACE_LOG", opt_value(&i, argc, argv, "--log"), 1);
    } else if (std::strncmp(arg, "--ftrace", 8) == 0) {
      setenv("NPC_FTRACE", "1", 1);
      setenv("NPC_FTRACE_ELF", opt_value(&i, argc, argv, "--ftrace"), 1);
    } else if (match_opt(arg, "-h", "--help")) {
      usage(argv[0]);
      std::exit(0);
    } else if (arg[0] == '-') {
      std::fprintf(stderr, "Unknown option: %s\n", arg);
      usage(argv[0]);
      std::exit(1);
    } else if (args.img_file == nullptr) {
      args.img_file = arg;
    } else {
      std::fprintf(stderr, "Unexpected argument: %s\n", arg);
      usage(argv[0]);
      std::exit(1);
    }
  }
  return args;
}

int main(int argc, char **argv) {
  Verilated::commandArgs(argc, argv);
  CmdArgs args = parse_args(argc, argv);
#ifdef NPC_ENABLE_NVBOARD
  const char *gui_env = std::getenv("NPC_GUI");
  if (gui_env != nullptr && std::strcmp(gui_env, "1") == 0) {
    gui_enabled = true;
  }
#endif

  std::memset(pmem, 0, sizeof(pmem));
  long img_size = load_img(args.img_file);

#ifdef NPC_ENABLE_NVBOARD
  if (gui_enabled) {
    nvboard_bind_all_pins(&dut);
    nvboard_init();
  }
#endif

  npc_trace_init(args.img_file);
  reset(10);
  npc_difftest_init(args.diff_so, img_size, args.diff_port);
  if (args.batch) {
    sdb_set_batch_mode();
  }
  sdb_mainloop();
  npc_trace_close();

#ifdef NPC_ENABLE_NVBOARD
  if (gui_enabled) {
    nvboard_quit();
  }
#endif

  return dut.trap && dut.trap_code != 0 ? 1 : 0;
}
