// The part of the event log that must survive a crash (evlog_crash.h): kept in RTC memory, which a panic or watchdog
// reset leaves alone. A panic notes its reason, PC and first backtrace addresses there (on_panic) before ESP-IDF's
// handler prints the backtrace and restarts.
#ifdef BOARD_TRMNL_X

#include "evlog_crash.h"

#include <esp32-hal.h>
#include <esp_attr.h>
#include <stdio.h>
#include <string.h>

#define RTC_MAGIC 0x52434C45u // "ELCR"
#define CRASH_MAGIC 0x48535243u // "CRSH": written by the panic handler only

typedef struct {
  uint32_t magic;
  uint16_t boot;
  uint8_t count, next; // pending events in the ring below
  evlog_pending_t pending[EVLOG_CRASH_PENDING];
  char app[32]; // the running app, "" when none
  uint32_t app_entry;
  uint32_t crash_magic;
  uint32_t pc, core, nbt;
  uint32_t bt[EVLOG_CRASH_BT]; // the first backtrace addresses
  char reason[32];
} rtc_log_t;

RTC_NOINIT_ATTR static rtc_log_t rtc;

static bool rtc_ok(void) { return rtc.magic == RTC_MAGIC && rtc.count <= EVLOG_CRASH_PENDING && rtc.next < EVLOG_CRASH_PENDING; }

static void rtc_reset(uint16_t boot) {
  memset(&rtc, 0, sizeof(rtc));
  rtc.magic = RTC_MAGIC;
  rtc.boot = boot;
}

void evlog_crash_pending(uint16_t boot, uint32_t ms, uint32_t wall, const char *text) {
  if (!rtc_ok()) rtc_reset(boot);
  evlog_pending_t *p = &rtc.pending[rtc.next];
  p->boot = boot;
  p->ms = ms;
  p->wall = wall;
  strncpy(p->text, text, sizeof(p->text) - 1);
  p->text[sizeof(p->text) - 1] = '\0';
  rtc.next = (uint8_t)((rtc.next + 1) % EVLOG_CRASH_PENDING);
  if (rtc.count < EVLOG_CRASH_PENDING) rtc.count++;
}

void evlog_crash_written(void) {
  if (rtc_ok()) rtc.count = 0;
}

void evlog_crash_app(const char *name, uint32_t entry) {
  if (!rtc_ok()) rtc_reset(0);
  strncpy(rtc.app, name ? name : "", sizeof(rtc.app) - 1);
  rtc.app[sizeof(rtc.app) - 1] = '\0';
  rtc.app_entry = name ? entry : 0;
}

// The app-relative offset of `pc` when it lies in the app's first 4 MB after its entry, for tools/app_addr2line.py
static int app_offset(char *out, size_t len, uint32_t pc) {
  uint32_t off = pc - rtc.app_entry;
  if (!rtc.app_entry || off >= (4u << 20)) return snprintf(out, len, " 0x%08lx", (unsigned long)pc);
  return snprintf(out, len, " app+0x%lx", (unsigned long)off);
}

int evlog_crash_take(bool crashed, evlog_pending_t *out, int max, char *crash, size_t crash_len, uint16_t *boot) {
  crash[0] = '\0';
  if (!rtc_ok()) {
    rtc_reset(0);
    return 0;
  }
  int n = 0;
  for (int i = 0; i < rtc.count && n < max; i++) { // oldest first
    int at = (rtc.next - rtc.count + i + EVLOG_CRASH_PENDING) % EVLOG_CRASH_PENDING;
    out[n] = rtc.pending[at];
    out[n].text[sizeof(out[n].text) - 1] = '\0';
    n++;
  }
  *boot = rtc.boot;
  if (crashed) {
    rtc.app[sizeof(rtc.app) - 1] = '\0';
    rtc.reason[sizeof(rtc.reason) - 1] = '\0';
    size_t at = 0;
    if (rtc.crash_magic == CRASH_MAGIC) {
      at = (size_t)snprintf(crash, crash_len, "crash: %s at", rtc.reason[0] ? rtc.reason : "panic");
      at += (size_t)app_offset(crash + at, at < crash_len ? crash_len - at : 0, rtc.pc);
      for (uint32_t k = 0; k < rtc.nbt && k < EVLOG_CRASH_BT && at < crash_len; k++) { // then the backtrace
        if (rtc.bt[k] != rtc.pc) at += (size_t)app_offset(crash + at, crash_len - at, rtc.bt[k]);
      }
    } else {
      at = (size_t)snprintf(crash, crash_len, "crash: no panic record (watchdog reset or brownout)");
    }
    if (at < crash_len) {
      if (rtc.app[0]) snprintf(crash + at, crash_len - at, ", app %s entry 0x%08lx", rtc.app, (unsigned long)rtc.app_entry);
      else snprintf(crash + at, crash_len - at, ", no app");
    }
  }
  uint16_t b = rtc.boot;
  rtc_reset(b);
  return n;
}

void evlog_crash_set_boot(uint16_t boot) {
  if (!rtc_ok()) rtc_reset(boot);
  rtc.boot = boot;
}

// ---- the panic handler's note (the Arduino core's hook in its esp_panic_handler wrap, before ESP-IDF's handler) ----
// IRAM, no flash code: the cache may be off. The reason text (e.g. "StoreProhibited", "abort()") is copied as is.

static void IRAM_ATTR on_panic(arduino_panic_info_t *info, void *arg) {
  (void)arg;
  if (!info) return;
  rtc.pc = (uint32_t)(uintptr_t)info->pc;
  rtc.core = (uint32_t)info->core;
  size_t i = 0;
  for (const char *c = info->reason; c && *c && i + 1 < sizeof(rtc.reason); c++) rtc.reason[i++] = *c;
  rtc.reason[i] = '\0';
  rtc.nbt = 0;
  for (unsigned k = 0; k < info->backtrace_len && rtc.nbt < EVLOG_CRASH_BT; k++) rtc.bt[rtc.nbt++] = info->backtrace[k];
  rtc.crash_magic = CRASH_MAGIC;
}

void evlog_crash_begin(void) { set_arduino_panic_handler(on_panic, nullptr); }

#endif // BOARD_TRMNL_X
