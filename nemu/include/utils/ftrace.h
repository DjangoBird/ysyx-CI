#ifndef __FTRACE_H__
#define __FTRACE_H__

#include <common.h>

void init_ftrace(const char *elf_file);
const char *ftrace_find_symbol(vaddr_t addr, vaddr_t *sym_addr);
void ftrace_record_call(vaddr_t pc, vaddr_t target);
void ftrace_record_ret(vaddr_t pc, vaddr_t target);

#endif
