#pragma once

#include <cstdint>

void npc_difftest_init(const char *ref_so_file, long img_size, int port);
void npc_difftest_step(uint32_t pc, uint32_t npc);
void npc_difftest_skip_ref(uint32_t npc);
bool npc_difftest_enabled();
