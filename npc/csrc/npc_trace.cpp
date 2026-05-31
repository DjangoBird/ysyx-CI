#include "npc_trace.h"

#include <capstone/capstone.h>
#include <elf.h>

#include <algorithm>
#include <cstdarg>
#include <cctype>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

static uint32_t current_pc = 0;
static uint32_t current_instr = 0;

static bool trace_on = false;
static bool itrace_on = false;
static bool mtrace_on = false;
static bool ftrace_on = false;
static FILE *trace_fp = nullptr;
static csh capstone_handle = 0;
static bool capstone_ready = false;

struct FuncSym {
  uint32_t addr = 0;
  uint32_t size = 0;
  std::string name;
};

static std::vector<FuncSym> funcs;
static int ftrace_depth = 0;

extern "C" void npc_set_current_instr(int pc, int instr) {
  current_pc = (uint32_t)pc;
  current_instr = (uint32_t)instr;
}

void npc_trace_get_current(uint32_t *pc, uint32_t *instr) {
  if (pc != nullptr) *pc = current_pc;
  if (instr != nullptr) *instr = current_instr;
}

static bool env_on(const char *name) {
  const char *v = std::getenv(name);
  if (v == nullptr) return false;
  return std::strcmp(v, "1") == 0 || std::strcmp(v, "true") == 0 || std::strcmp(v, "on") == 0;
}

static void trace_printf(const char *fmt, ...) {
  if (!trace_on) return;
  va_list ap;
  va_start(ap, fmt);
  vfprintf(trace_fp != nullptr ? trace_fp : stdout, fmt, ap);
  va_end(ap);
  if (trace_fp != nullptr) fflush(trace_fp);
}

static std::vector<uint8_t> read_file(const char *path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  in.seekg(0, std::ios::end);
  std::streamoff size = in.tellg();
  if (size <= 0) return {};
  in.seekg(0, std::ios::beg);
  std::vector<uint8_t> data((size_t)size);
  in.read((char *)data.data(), size);
  return data;
}

static const char *cstring_at(const std::vector<uint8_t> &data, size_t off) {
  if (off >= data.size()) return "";
  return (const char *)data.data() + off;
}

static void load_ftrace_elf32(const std::vector<uint8_t> &data, const Elf32_Ehdr *eh) {
  if (eh->e_shoff == 0 || eh->e_shentsize < sizeof(Elf32_Shdr)) return;
  if ((size_t)eh->e_shoff + (size_t)eh->e_shnum * eh->e_shentsize > data.size()) return;

  const auto *shdrs = (const Elf32_Shdr *)(data.data() + eh->e_shoff);
  for (int i = 0; i < eh->e_shnum; ++i) {
    const Elf32_Shdr &symsec = shdrs[i];
    if (symsec.sh_type != SHT_SYMTAB && symsec.sh_type != SHT_DYNSYM) continue;
    if (symsec.sh_entsize < sizeof(Elf32_Sym)) continue;
    if (symsec.sh_link >= eh->e_shnum) continue;
    if ((size_t)symsec.sh_offset + symsec.sh_size > data.size()) continue;

    const Elf32_Shdr &strsec = shdrs[symsec.sh_link];
    if ((size_t)strsec.sh_offset + strsec.sh_size > data.size()) continue;

    size_t count = symsec.sh_size / symsec.sh_entsize;
    for (size_t j = 0; j < count; ++j) {
      const auto *sym = (const Elf32_Sym *)(data.data() + symsec.sh_offset + j * symsec.sh_entsize);
      if (ELF32_ST_TYPE(sym->st_info) != STT_FUNC) continue;
      if (sym->st_value == 0 || sym->st_name >= strsec.sh_size) continue;
      const char *name = cstring_at(data, strsec.sh_offset + sym->st_name);
      if (name[0] == '\0') continue;
      funcs.push_back({sym->st_value, sym->st_size, name});
    }
  }
}

static void load_ftrace_elf64(const std::vector<uint8_t> &data, const Elf64_Ehdr *eh) {
  if (eh->e_shoff == 0 || eh->e_shentsize < sizeof(Elf64_Shdr)) return;
  if ((size_t)eh->e_shoff + (size_t)eh->e_shnum * eh->e_shentsize > data.size()) return;

  const auto *shdrs = (const Elf64_Shdr *)(data.data() + eh->e_shoff);
  for (int i = 0; i < eh->e_shnum; ++i) {
    const Elf64_Shdr &symsec = shdrs[i];
    if (symsec.sh_type != SHT_SYMTAB && symsec.sh_type != SHT_DYNSYM) continue;
    if (symsec.sh_entsize < sizeof(Elf64_Sym)) continue;
    if (symsec.sh_link >= eh->e_shnum) continue;
    if ((size_t)symsec.sh_offset + symsec.sh_size > data.size()) continue;

    const Elf64_Shdr &strsec = shdrs[symsec.sh_link];
    if ((size_t)strsec.sh_offset + strsec.sh_size > data.size()) continue;

    size_t count = symsec.sh_size / symsec.sh_entsize;
    for (size_t j = 0; j < count; ++j) {
      const auto *sym = (const Elf64_Sym *)(data.data() + symsec.sh_offset + j * symsec.sh_entsize);
      if (ELF64_ST_TYPE(sym->st_info) != STT_FUNC) continue;
      if (sym->st_value == 0 || sym->st_name >= strsec.sh_size) continue;
      const char *name = cstring_at(data, strsec.sh_offset + sym->st_name);
      if (name[0] == '\0') continue;
      funcs.push_back({(uint32_t)sym->st_value, (uint32_t)sym->st_size, name});
    }
  }
}

static void load_ftrace_symbols(const char *elf_path) {
  if (elf_path == nullptr || elf_path[0] == '\0') return;
  std::vector<uint8_t> data = read_file(elf_path);
  if (data.size() < EI_NIDENT || std::memcmp(data.data(), ELFMAG, SELFMAG) != 0) {
    trace_printf("ftrace: cannot read ELF symbols from %s\n", elf_path);
    return;
  }
  if (data[EI_CLASS] == ELFCLASS32 && data.size() >= sizeof(Elf32_Ehdr)) {
    load_ftrace_elf32(data, (const Elf32_Ehdr *)data.data());
  } else if (data[EI_CLASS] == ELFCLASS64 && data.size() >= sizeof(Elf64_Ehdr)) {
    load_ftrace_elf64(data, (const Elf64_Ehdr *)data.data());
  }
  std::sort(funcs.begin(), funcs.end(), [](const FuncSym &a, const FuncSym &b) { return a.addr < b.addr; });
  if (!funcs.empty()) trace_printf("ftrace: loaded %zu function symbols from %s\n", funcs.size(), elf_path);
}

static const FuncSym *find_func(uint32_t addr) {
  const FuncSym *best = nullptr;
  for (const auto &fn : funcs) {
    if (fn.addr > addr) break;
    uint32_t end = fn.size == 0 ? fn.addr + 4 : fn.addr + fn.size;
    if (addr >= fn.addr && addr < end) best = &fn;
  }
  return best;
}

static std::string func_name(uint32_t addr) {
  const FuncSym *fn = find_func(addr);
  char buf[32];
  if (fn == nullptr) {
    std::snprintf(buf, sizeof(buf), "0x%08x", addr);
    return buf;
  }
  char out[256];
  std::snprintf(out, sizeof(out), "%s@0x%08x", fn->name.c_str(), fn->addr);
  return out;
}

static void init_capstone() {
  if (!itrace_on) return;
  if (cs_open(CS_ARCH_RISCV, (cs_mode)(CS_MODE_RISCV32 | CS_MODE_RISCVC), &capstone_handle) == CS_ERR_OK) {
    capstone_ready = true;
  } else {
    trace_printf("itrace: failed to initialize capstone, fallback to raw instruction words\n");
  }
}

void npc_trace_init(const char *img_file) {
  trace_on = env_on("NPC_TRACE") || env_on("NPC_ITRACE") || env_on("NPC_MTRACE") || env_on("NPC_FTRACE");
  itrace_on = env_on("NPC_TRACE") || env_on("NPC_ITRACE");
  mtrace_on = env_on("NPC_TRACE") || env_on("NPC_MTRACE");
  ftrace_on = env_on("NPC_TRACE") || env_on("NPC_FTRACE");

  const char *log_path = std::getenv("NPC_TRACE_LOG");
  if (trace_on && log_path != nullptr && log_path[0] != '\0') {
    trace_fp = std::fopen(log_path, "w");
    if (trace_fp == nullptr) {
      std::perror("Failed to open NPC_TRACE_LOG");
      trace_fp = stdout;
    }
  }

  init_capstone();

  const char *elf_path = std::getenv("NPC_FTRACE_ELF");
  if (ftrace_on && elf_path != nullptr && elf_path[0] != '\0') {
    load_ftrace_symbols(elf_path);
  }
}

void npc_trace_close() {
  if (capstone_ready) {
    cs_close(&capstone_handle);
    capstone_ready = false;
  }
  if (trace_fp != nullptr && trace_fp != stdout) {
    std::fclose(trace_fp);
  }
  trace_fp = nullptr;
}

static void write_inst_bytes(char *buf, size_t size, uint32_t instr) {
  std::snprintf(buf, size, "%02x %02x %02x %02x", instr & 0xff, (instr >> 8) & 0xff,
                (instr >> 16) & 0xff, (instr >> 24) & 0xff);
}

static void trace_itrace(uint32_t pc, uint32_t instr) {
  if (!itrace_on) return;

  char bytes[32];
  write_inst_bytes(bytes, sizeof(bytes), instr);

  std::string asm_text;
  if (capstone_ready) {
    cs_insn *insn = nullptr;
    size_t count = cs_disasm(capstone_handle, (const uint8_t *)&instr, 4, pc, 1, &insn);
    if (count > 0) {
      asm_text = insn[0].mnemonic;
      if (insn[0].op_str[0] != '\0') {
        asm_text += "\t";
        asm_text += insn[0].op_str;
      }
      cs_free(insn, count);
    }
  }
  if (asm_text.empty()) {
    char fallback[32];
    std::snprintf(fallback, sizeof(fallback), ".word\t0x%08x", instr);
    asm_text = fallback;
  }

  trace_printf("itrace 0x%08x: %-11s %s\n", pc, bytes, asm_text.c_str());
}

static int store_len(uint8_t mask) {
  int len = 0;
  for (int i = 0; i < 4; ++i) if (mask & (1u << i)) ++len;
  return len == 0 ? 4 : len;
}

static void trace_mtrace(bool mem_valid, bool mem_we, uint8_t mem_wmask,
                         uint32_t mem_addr, uint32_t mem_wdata, uint32_t mem_rdata) {
  if (!mtrace_on || !mem_valid) return;
  if (mem_we) {
    trace_printf("mtrace W addr=0x%08x len=%d data=0x%08x mask=0x%x\n",
                 mem_addr, store_len(mem_wmask), mem_wdata, mem_wmask);
  } else {
    trace_printf("mtrace R addr=0x%08x len=4 data=0x%08x\n", mem_addr, mem_rdata);
  }
}

static uint32_t imm_i(uint32_t inst) {
  uint32_t imm = inst >> 20;
  if (imm & 0x800) imm |= 0xfffff000u;
  return imm;
}

static void trace_ftrace(uint32_t pc, uint32_t instr, uint32_t next_pc) {
  if (!ftrace_on) return;
  uint32_t opcode = instr & 0x7f;
  uint32_t rd = (instr >> 7) & 0x1f;
  uint32_t funct3 = (instr >> 12) & 0x7;
  uint32_t rs1 = (instr >> 15) & 0x1f;

  bool is_jal = opcode == 0x6f;
  bool is_jalr = opcode == 0x67 && funct3 == 0;
  bool is_ret = is_jalr && rd == 0 && (rs1 == 1 || rs1 == 5) && imm_i(instr) == 0;
  bool is_call = (is_jal || is_jalr) && (rd == 1 || rd == 5) && next_pc != pc + 4;

  if (is_ret) {
    if (ftrace_depth > 0) --ftrace_depth;
    std::string name = func_name(next_pc);
    trace_printf("%*s0x%08x: ret  [%s]\n", ftrace_depth * 2, "", pc, name.c_str());
  } else if (is_call) {
    std::string name = func_name(next_pc);
    trace_printf("%*s0x%08x: call [%s]\n", ftrace_depth * 2, "", pc, name.c_str());
    ++ftrace_depth;
  }
}

void npc_trace_commit(uint32_t pc, uint32_t instr, uint32_t next_pc,
                      bool mem_valid, bool mem_we, uint8_t mem_wmask,
                      uint32_t mem_addr, uint32_t mem_wdata, uint32_t mem_rdata) {
  if (!trace_on) return;
  trace_itrace(pc, instr);
  trace_mtrace(mem_valid, mem_we, mem_wmask, mem_addr, mem_wdata, mem_rdata);
  trace_ftrace(pc, instr, next_pc);
}
