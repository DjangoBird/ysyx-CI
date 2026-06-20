#include <am.h>
#include <klib.h>
#include <klib-macros.h>

extern char _heap_start;
int main(const char *args);

extern char _pmem_start;
#define PMEM_SIZE (128 * 1024 * 1024)
#define PMEM_END  ((uintptr_t)&_pmem_start + PMEM_SIZE)

Area heap = RANGE(&_heap_start, PMEM_END);
static const char mainargs[MAINARGS_MAX_LEN] = TOSTRING(MAINARGS_PLACEHOLDER); // defined in CFLAGS

void putch(char ch) {
  *(volatile uint8_t *)0xa00003f8 = ch;
}

void halt(int code) {
  asm volatile("mv a0, %0; ebreak" : : "r"(code));

  // should not reach here
  while (1);
}

void _trm_init() {
  uintptr_t vendor = 0, arch = 0;
  asm volatile("csrr %0, mvendorid" : "=r"(vendor));
  asm volatile("csrr %0, marchid" : "=r"(arch));
  printf("mvendorid = 0x%x, marchid = %u\n", vendor, arch);
  int ret = main(mainargs);
  halt(ret);
}
