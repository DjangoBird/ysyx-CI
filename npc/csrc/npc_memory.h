#pragma once

#include <cstdint>

bool in_pmem(uint32_t addr);
uint32_t pmem_off(uint32_t addr);
uint32_t pmem_read32(uint32_t addr);
void pmem_write32(uint32_t addr, uint32_t data, uint8_t wmask);
long load_img(const char *img_file);
