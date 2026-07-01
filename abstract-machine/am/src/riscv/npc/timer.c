#include <am.h>

#define NPC_MTIME_ADDR 0xa0000048u

#ifndef NPC_CLOCK_FREQ
#define NPC_CLOCK_FREQ 100000000ull
#endif

static uint64_t boot_time = 0;

static uint64_t read_mtime() {
  volatile uint32_t *mtime = (volatile uint32_t *)NPC_MTIME_ADDR;
  uint32_t hi1, lo, hi2;
  do {
    hi1 = mtime[1];
    lo = mtime[0];
    hi2 = mtime[1];
  } while (hi1 != hi2);
  return ((uint64_t)hi1 << 32) | lo;
}

void __am_timer_init() {
  boot_time = read_mtime();
}

void __am_timer_uptime(AM_TIMER_UPTIME_T *uptime) {
  uint64_t cycles = read_mtime() - boot_time;
  uptime->us = (cycles / NPC_CLOCK_FREQ) * 1000000ull +
               (cycles % NPC_CLOCK_FREQ) * 1000000ull / NPC_CLOCK_FREQ;
}

void __am_timer_rtc(AM_TIMER_RTC_T *rtc) {
  rtc->second = 0;
  rtc->minute = 0;
  rtc->hour   = 0;
  rtc->day    = 0;
  rtc->month  = 0;
  rtc->year   = 1900;
}
