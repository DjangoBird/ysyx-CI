/* Simple ELF symbol parser for function tracing (ftrace) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <elf.h>
#include <common.h>
#include "utils/ftrace.h"
#include "utils.h"

typedef struct {
  uint64_t addr;
  uint64_t size;
  char *name;
} FuncSym;

static FuncSym *funcs = NULL;
static size_t func_cnt = 0;

static void add_func(uint64_t addr, uint64_t size, const char *name) {
  FuncSym *tmp = realloc(funcs, sizeof(FuncSym) * (func_cnt + 1));
  if (!tmp) return;
  funcs = tmp;
  funcs[func_cnt].addr = addr;
  funcs[func_cnt].size = size;
  funcs[func_cnt].name = strdup(name ? name : "");
  func_cnt++;
}

static int cmp_func(const void *a, const void *b) {
  const FuncSym *fa = a;
  const FuncSym *fb = b;
  if (fa->addr < fb->addr) return -1;
  if (fa->addr > fb->addr) return 1;
  return 0;
}

void init_ftrace(const char *elf_file) {
  if (!elf_file) return;
  FILE *f = fopen(elf_file, "rb");
  if (!f) {
    Log("ftrace: can not open ELF file %s", elf_file);
    return;
  }

  unsigned char e_ident[EI_NIDENT];
  if (fread(e_ident, 1, EI_NIDENT, f) != EI_NIDENT) goto out;
  fseek(f, 0, SEEK_SET);

  if (e_ident[EI_CLASS] == ELFCLASS32) {
    Elf32_Ehdr eh;
    if (fread(&eh, 1, sizeof(eh), f) != sizeof(eh)) goto out;
    Elf32_Shdr *shdrs = malloc(eh.e_shnum * sizeof(Elf32_Shdr));
    if (!shdrs) goto out;
    fseek(f, eh.e_shoff, SEEK_SET);
    {
      size_t r = fread(shdrs, sizeof(Elf32_Shdr), eh.e_shnum, f);
      if (r != (size_t)eh.e_shnum) { free(shdrs); goto out; }
    }

    // section header string table
    Elf32_Shdr shstr = shdrs[eh.e_shstrndx];
    char *shstrtab = malloc(shstr.sh_size);
    if (!shstrtab) { free(shdrs); goto out; }
    fseek(f, shstr.sh_offset, SEEK_SET);
    {
      size_t r = fread(shstrtab, 1, shstr.sh_size, f);
      if (r != (size_t)shstr.sh_size) { free(shstrtab); free(shdrs); goto out; }
    }

    for (int i = 0; i < eh.e_shnum; i++) {
      if (shdrs[i].sh_type == SHT_SYMTAB) {
        Elf32_Shdr sym_sh = shdrs[i];
        Elf32_Shdr str_sh = shdrs[sym_sh.sh_link];
        char *strtab = malloc(str_sh.sh_size);
        if (!strtab) continue;
        fseek(f, str_sh.sh_offset, SEEK_SET);
        {
          size_t r = fread(strtab, 1, str_sh.sh_size, f);
          if (r != (size_t)str_sh.sh_size) { free(strtab); continue; }
        }

        int n = sym_sh.sh_size / sym_sh.sh_entsize;
        fseek(f, sym_sh.sh_offset, SEEK_SET);
        for (int j = 0; j < n; j++) {
          Elf32_Sym sym;
          {
            size_t r = fread(&sym, 1, sizeof(sym), f);
            if (r != sizeof(sym)) { goto out; }
          }
          unsigned char type = ELF32_ST_TYPE(sym.st_info);
          if (type == STT_FUNC && sym.st_value != 0) {
            const char *name = (sym.st_name < str_sh.sh_size) ? (strtab + sym.st_name) : "";
            add_func((uint64_t)sym.st_value, (uint64_t)sym.st_size, name);
          }
        }
        free(strtab);
      }
    }

    free(shstrtab);
    free(shdrs);
  } else if (e_ident[EI_CLASS] == ELFCLASS64) {
    Elf64_Ehdr eh;
    if (fread(&eh, 1, sizeof(eh), f) != sizeof(eh)) goto out;
    Elf64_Shdr *shdrs = malloc(eh.e_shnum * sizeof(Elf64_Shdr));
    if (!shdrs) goto out;
    fseek(f, eh.e_shoff, SEEK_SET);
    {
      size_t r = fread(shdrs, sizeof(Elf64_Shdr), eh.e_shnum, f);
      if (r != (size_t)eh.e_shnum) { free(shdrs); goto out; }
    }

    Elf64_Shdr shstr = shdrs[eh.e_shstrndx];
    char *shstrtab = malloc(shstr.sh_size);
    if (!shstrtab) { free(shdrs); goto out; }
    fseek(f, shstr.sh_offset, SEEK_SET);
    {
      size_t r = fread(shstrtab, 1, shstr.sh_size, f);
      if (r != (size_t)shstr.sh_size) { free(shstrtab); free(shdrs); goto out; }
    }

    for (int i = 0; i < eh.e_shnum; i++) {
      if (shdrs[i].sh_type == SHT_SYMTAB) {
        Elf64_Shdr sym_sh = shdrs[i];
        Elf64_Shdr str_sh = shdrs[sym_sh.sh_link];
        char *strtab = malloc(str_sh.sh_size);
        if (!strtab) continue;
        fseek(f, str_sh.sh_offset, SEEK_SET);
        {
          size_t r = fread(strtab, 1, str_sh.sh_size, f);
          if (r != (size_t)str_sh.sh_size) { free(strtab); continue; }
        }

        int n = sym_sh.sh_size / sym_sh.sh_entsize;
        fseek(f, sym_sh.sh_offset, SEEK_SET);
        for (int j = 0; j < n; j++) {
          Elf64_Sym sym;
          {
            size_t r = fread(&sym, 1, sizeof(sym), f);
            if (r != sizeof(sym)) { goto out; }
          }
          unsigned char type = ELF64_ST_TYPE(sym.st_info);
          if (type == STT_FUNC && sym.st_value != 0) {
            const char *name = (sym.st_name < str_sh.sh_size) ? (strtab + sym.st_name) : "";
            add_func((uint64_t)sym.st_value, (uint64_t)sym.st_size, name);
          }
        }
        free(strtab);
      }
    }

    free(shstrtab);
    free(shdrs);
  } else {
    Log("ftrace: unsupported ELF class %d", e_ident[EI_CLASS]);
  }

  if (func_cnt > 1) qsort(funcs, func_cnt, sizeof(FuncSym), cmp_func);
  Log("ftrace: loaded %zu function symbols from %s", func_cnt, elf_file);

out:
  fclose(f);
}

const char *ftrace_find_symbol(vaddr_t addr, vaddr_t *sym_addr) {
  for (size_t i = 0; i < func_cnt; i++) {
    if (addr >= funcs[i].addr && addr < funcs[i].addr + funcs[i].size) {
      if (sym_addr) *sym_addr = funcs[i].addr;
      return funcs[i].name;
    }
  }
  return "???";
}

static int ftrace_depth = 0;

void ftrace_record_call(vaddr_t pc, vaddr_t target) {
  vaddr_t symaddr = 0;
  const char *name = ftrace_find_symbol(target, &symaddr);
  (void)name;
  // indent
  char ind[64] = {0};
  int n = ftrace_depth < 60 ? ftrace_depth : 60;
  memset(ind, ' ', n);
  ind[n] = '\0';
  log_write("%s" FMT_WORD ": call [%s@" FMT_WORD "]\n", ind, pc, name, symaddr);
  ftrace_depth++;
}

void ftrace_record_ret(vaddr_t pc, vaddr_t target) {
  if (ftrace_depth > 0) ftrace_depth--;
  vaddr_t symaddr = 0;
  const char *name = ftrace_find_symbol(target, &symaddr);
  (void)name;
  char ind[64] = {0};
  int n = ftrace_depth < 60 ? ftrace_depth : 60;
  memset(ind, ' ', n);
  ind[n] = '\0';
  log_write("%s" FMT_WORD ":   ret  [%s]\n", ind, pc, name);
}
