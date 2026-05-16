/***************************************************************************************
* Copyright (c) 2014-2024 Zihao Yu, Nanjing University
*
* NEMU is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*          http://license.coscl.org.cn/MulanPSL2
*
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
*
* See the Mulan PSL v2 for more details.
***************************************************************************************/

#include <cpu/cpu.h>
#include <cpu/decode.h>
#include <cpu/difftest.h>
#include <locale.h>
#include "monitor/sdb/sdb.h"
#ifdef CONFIG_FTRACE
#include "utils/ftrace.h"
#endif

/* The assembly code of instructions executed is only output to the screen
 * when the number of instructions executed is less than this value.
 * This is useful when you use the `si' command.
 * You can modify this value as you want.
 */
#define MAX_INST_TO_PRINT 10
#define LOOP_TRACE_SIZE 32
#define LOOP_MAX_PERIOD 16
#define LOOP_DEFAULT_THRESHOLD 8

CPU_state cpu = {};
uint64_t g_nr_guest_inst = 0;
static uint64_t g_timer = 0; // unit: us
static bool g_print_step = false;
static int loop_detect_threshold = LOOP_DEFAULT_THRESHOLD;

#ifdef CONFIG_ITRACE
#define IRINGBUF_SIZE 32
static char iringbuf[IRINGBUF_SIZE][128];
static int iringbuf_head = 0;
static int iringbuf_cnt = 0;

static void iringbuf_record(const char *str) {
  strcpy(iringbuf[iringbuf_head], str);
  iringbuf_head = (iringbuf_head + 1) % IRINGBUF_SIZE;
  if (iringbuf_cnt < IRINGBUF_SIZE) {
    iringbuf_cnt++;
  }
}

static void iringbuf_display() {
  if (iringbuf_cnt == 0) {
    return;
  }

  puts("Instruction ring buffer:");
  int start = (iringbuf_head - iringbuf_cnt + IRINGBUF_SIZE) % IRINGBUF_SIZE;
  for (int i = 0; i < iringbuf_cnt; i++) {
    puts(iringbuf[(start + i) % IRINGBUF_SIZE]);
  }
}
#endif

static vaddr_t loop_trace[LOOP_TRACE_SIZE];
static int loop_trace_head = 0;
static int loop_trace_cnt = 0;

static void loop_trace_record(vaddr_t pc) {
  loop_trace[loop_trace_head] = pc;
  loop_trace_head = (loop_trace_head + 1) % LOOP_TRACE_SIZE;
  if (loop_trace_cnt < LOOP_TRACE_SIZE) {
    loop_trace_cnt++;
  }
}

static vaddr_t loop_trace_get(int back) {
  return loop_trace[(loop_trace_head + LOOP_TRACE_SIZE - 1 - back) % LOOP_TRACE_SIZE];
}

static bool loop_chunk_equal(int period, int chunk_a, int chunk_b) {
  for (int i = 0; i < period; i++) {
    if (loop_trace_get(chunk_a * period + i) != loop_trace_get(chunk_b * period + i)) {
      return false;
    }
  }
  return true;
}

static bool detect_loop(int *out_period, int *out_repeat) {
  if (loop_detect_threshold <= 0) {
    return false;
  }

  int max_period = LOOP_MAX_PERIOD < (loop_trace_cnt / loop_detect_threshold)
                     ? LOOP_MAX_PERIOD : (loop_trace_cnt / loop_detect_threshold);
  if (max_period <= 0) {
    return false;
  }

  for (int period = 1; period <= max_period; period++) {
    int needed = period * loop_detect_threshold;
    if (loop_trace_cnt < needed) {
      continue;
    }

    bool repeated = true;
    for (int rep = 1; rep < loop_detect_threshold; rep++) {
      if (!loop_chunk_equal(period, rep - 1, rep)) {
        repeated = false;
        break;
      }
    }

    if (repeated) {
      *out_period = period;
      *out_repeat = loop_detect_threshold;
      return true;
    }
  }

  return false;
}

static void report_possible_loop(vaddr_t pc, int period, int repeat) {
  Log("Possible infinite loop detected at pc = " FMT_WORD ", pattern period = %d, repeat = %d",
      pc, period, repeat);
#ifdef CONFIG_ITRACE
  puts("Recent instructions:");
  iringbuf_display();
#else
  puts("Enable ITRACE to print recent instruction trace.");
#endif
  nemu_state.state = NEMU_STOP;
}

void device_update();

static void trace_and_difftest(Decode *_this, vaddr_t dnpc) {
#ifdef CONFIG_ITRACE_COND
  if (ITRACE_COND) { log_write("%s\n", _this->logbuf); }
#endif
#ifdef CONFIG_ITRACE
  iringbuf_record(_this->logbuf);
#endif
  if (g_print_step) { IFDEF(CONFIG_ITRACE, puts(_this->logbuf)); }
  IFDEF(CONFIG_DIFFTEST, difftest_step(_this->pc, dnpc));

  // 每执行完一条指令后检查监视点(可通过 CONFIG_WATCHPOINT 开关控制)
  IFDEF(CONFIG_WATCHPOINT, check_watchpoints());
}

static void exec_once(Decode *s, vaddr_t pc) {
  s->pc = pc;
  s->snpc = pc;
  isa_exec_once(s);
  cpu.pc = s->dnpc;
#ifdef CONFIG_ITRACE
  char *p = s->logbuf;
  p += snprintf(p, sizeof(s->logbuf), FMT_WORD ":", s->pc);
  int ilen = s->snpc - s->pc;
  int i;
  uint8_t *inst = (uint8_t *)&s->isa.inst;
#ifdef CONFIG_ISA_x86
  for (i = 0; i < ilen; i ++) {
#else
  for (i = ilen - 1; i >= 0; i --) {
#endif
    p += snprintf(p, 4, " %02x", inst[i]);
  }
  int ilen_max = MUXDEF(CONFIG_ISA_x86, 8, 4);
  int space_len = ilen_max - ilen;
  if (space_len < 0) space_len = 0;
  space_len = space_len * 3 + 1;
  memset(p, ' ', space_len);
  p += space_len;

  void disassemble(char *str, int size, uint64_t pc, uint8_t *code, int nbyte);
  disassemble(p, s->logbuf + sizeof(s->logbuf) - p,
      MUXDEF(CONFIG_ISA_x86, s->snpc, s->pc), (uint8_t *)&s->isa.inst, ilen);
#ifdef CONFIG_FTRACE
  // detect call/ret by looking at disassembly mnemonic
  if (s->logbuf[0]) {
    if (strstr(s->logbuf, "call") || strstr(s->logbuf, "jalr") || strstr(s->logbuf, "jal")) {
      ftrace_record_call(s->pc, s->dnpc);
    } else if (strstr(s->logbuf, "ret")) {
      ftrace_record_ret(s->pc, s->dnpc);
    }
  }
#endif
#endif
}

static void execute(uint64_t n) {
  Decode s;
  for (;n > 0; n --) {
    vaddr_t pc = cpu.pc;
    loop_trace_record(pc);
    exec_once(&s, cpu.pc);
    g_nr_guest_inst ++;
    trace_and_difftest(&s, cpu.pc);

    int loop_period = 0;
    int loop_repeat = 0;
    if (detect_loop(&loop_period, &loop_repeat)) {
      report_possible_loop(pc, loop_period, loop_repeat);
      break;
    }

    if (nemu_state.state != NEMU_RUNNING) break;
    IFDEF(CONFIG_DEVICE, device_update());
  }
}

static void statistic() {
  IFNDEF(CONFIG_TARGET_AM, setlocale(LC_NUMERIC, ""));
#define NUMBERIC_FMT MUXDEF(CONFIG_TARGET_AM, "%", "%'") PRIu64
  Log("host time spent = " NUMBERIC_FMT " us", g_timer);
  Log("total guest instructions = " NUMBERIC_FMT, g_nr_guest_inst);
  if (g_timer > 0) Log("simulation frequency = " NUMBERIC_FMT " inst/s", g_nr_guest_inst * 1000000 / g_timer);
  else Log("Finish running in less than 1 us and can not calculate the simulation frequency");
}

void assert_fail_msg() {
  isa_reg_display();
  IFDEF(CONFIG_ITRACE, iringbuf_display());
  statistic();
}

/* Simulate how the CPU works. */
void cpu_exec(uint64_t n) {
  g_print_step = (n < MAX_INST_TO_PRINT);
  switch (nemu_state.state) {
    case NEMU_END: case NEMU_ABORT: case NEMU_QUIT:
      printf("Program execution has ended. To restart the program, exit NEMU and run again.\n");
      return;
    default: nemu_state.state = NEMU_RUNNING;
  }

  uint64_t timer_start = get_time();

  execute(n);

  uint64_t timer_end = get_time();
  g_timer += timer_end - timer_start;

  switch (nemu_state.state) {
    case NEMU_RUNNING: nemu_state.state = NEMU_STOP; break;

    case NEMU_END: case NEMU_ABORT:
      Log("nemu: %s at pc = " FMT_WORD,
          (nemu_state.state == NEMU_ABORT ? ANSI_FMT("ABORT", ANSI_FG_RED) :
           (nemu_state.halt_ret == 0 ? ANSI_FMT("HIT GOOD TRAP", ANSI_FG_GREEN) :
            ANSI_FMT("HIT BAD TRAP", ANSI_FG_RED))),
          nemu_state.halt_pc);
      // fall through
    case NEMU_QUIT: statistic();
  }
}

void set_loop_detect_threshold(int threshold) {
  loop_detect_threshold = threshold;
}

int get_loop_detect_threshold(void) {
  return loop_detect_threshold;
}
