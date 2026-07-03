#include "npc_memory.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "npc_runtime.h"

bool in_pmem(uint32_t addr) {
  return addr >= PMEM_BASE && addr < PMEM_BASE + PMEM_SIZE;
}

uint32_t pmem_off(uint32_t addr) {
  return addr - PMEM_BASE;
}

uint32_t pmem_read32(uint32_t addr) {
  if (!in_pmem(addr) || !in_pmem(addr + 3)) return 0;
  uint32_t off = pmem_off(addr);
  return (uint32_t)pmem[off] |
         ((uint32_t)pmem[off + 1] << 8) |
         ((uint32_t)pmem[off + 2] << 16) |
         ((uint32_t)pmem[off + 3] << 24);
}

void pmem_write32(uint32_t addr, uint32_t data, uint8_t wmask) {
  if (!in_pmem(addr) || !in_pmem(addr + 3)) return;
  uint32_t off = pmem_off(addr);
  if (wmask & 0x1) pmem[off] = data & 0xff;
  if (wmask & 0x2) pmem[off + 1] = (data >> 8) & 0xff;
  if (wmask & 0x4) pmem[off + 2] = (data >> 16) & 0xff;
  if (wmask & 0x8) pmem[off + 3] = (data >> 24) & 0xff;
}

long load_img(const char *img_file) {
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
