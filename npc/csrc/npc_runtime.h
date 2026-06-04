#pragma once

#include <cstdint>
#include "Vtop.h"

extern Vtop dut;
extern bool gui_enabled;

static constexpr uint32_t PMEM_SIZE = 128u * 1024u * 1024u;
static constexpr uint32_t PMEM_BASE = 0x80000000u;

extern uint8_t pmem[PMEM_SIZE];
