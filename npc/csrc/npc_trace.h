#pragma once

#include <cstdint>

void npc_trace_init(const char *img_file);
void npc_trace_close();
void npc_trace_get_current(uint32_t *pc, uint32_t *instr);
void npc_trace_commit(uint32_t pc, uint32_t instr, uint32_t next_pc,
                      bool mem_valid, bool mem_we, uint8_t mem_wmask,
                      uint32_t mem_addr, uint32_t mem_wdata, uint32_t mem_rdata);
