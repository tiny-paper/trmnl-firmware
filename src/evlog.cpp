// The X's event log: the ring of evlog_ring.h in RAM, written to /evlog.bin by its own task (evlog.h).
#ifdef BOARD_TRMNL_X

#include "evlog.h"
#include "evlog_crash.h"
#include "serial_out_hw.h"

#include <Arduino.h>
#include <app_api.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <evlog_ring.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdarg.h>
#include <time.h>

#define EVLOG_FILE "/evlog.bin"
#define EVLOG_TASK_POLL_MS 1000 // a deferred write goes out within this after its gap

static evlog_ring_t ring;
static uint8_t *snap; // the image being written (EVLOG_BYTES)
static SemaphoreHandle_t mu; // the ring
static SemaphoreHandle_t wmu; // snap and the file
static TaskHandle_t task;
static volatile bool closed; // after evlog_flush(true): the file system goes down

static uint8_t *alloc(size_t n) {
  uint8_t *p = (uint8_t *)heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : (uint8_t *)heap_caps_malloc(n, MALLOC_CAP_8BIT);
}

static const char *reset_str(esp_reset_reason_t r) {
  switch (r) {
  case ESP_RST_POWERON: return "power on";
  case ESP_RST_EXT: return "reset pin";
  case ESP_RST_SW: return "software restart";
  case ESP_RST_PANIC: return "crash (panic)";
  case ESP_RST_INT_WDT: return "interrupt watchdog";
  case ESP_RST_TASK_WDT: return "task watchdog";
  case ESP_RST_WDT: return "watchdog";
  case ESP_RST_DEEPSLEEP: return "deep sleep wake";
  case ESP_RST_BROWNOUT: return "brownout (battery low?)";
  case ESP_RST_USB: return "USB reset (flashing)";
  default: return "other";
  }
}

// Writes snap when the ring changed: always now (`force`) or when its gap is over. Holds wmu
static void write_out(bool force) {
  xSemaphoreTake(wmu, portMAX_DELAY);
  bool go = false;
  xSemaphoreTake(mu, portMAX_DELAY);
  if (!closed && (force ? ring.dirty : evlog_ring_flush_due(&ring, millis()))) {
    go = evlog_ring_save(&ring, snap, EVLOG_BYTES) == EVLOG_BYTES;
    evlog_ring_flushed(&ring, millis());
    if (go) evlog_crash_written(); // in the file from now on, not needed in RTC memory
  }
  xSemaphoreGive(mu);
  if (go) {
    File f = LittleFS.open(EVLOG_FILE, "w");
    size_t n = f ? f.write(snap, EVLOG_BYTES) : 0;
    if (f) f.close();
    if (n != EVLOG_BYTES) {
      Log_error("Event log: write failed (%u of %u bytes)", (unsigned)n, (unsigned)EVLOG_BYTES);
      xSemaphoreTake(mu, portMAX_DELAY);
      ring.dirty = true; // the next gap tries again
      xSemaphoreGive(mu);
    }
  }
  xSemaphoreGive(wmu);
}

// A failed allocation anywhere: noted without a lock here (any task, any state), logged by the flush task
static volatile uint32_t oom_count, oom_size, oom_caps;

static void on_alloc_failed(size_t size, uint32_t caps, const char *function) {
  (void)function;
  oom_size = (uint32_t)size;
  oom_caps = caps;
  oom_count = oom_count + 1;
}

static void flusher(void *arg) {
  (void)arg;
  uint32_t oom_seen = 0;
  while (true) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(EVLOG_TASK_POLL_MS));
    uint32_t n = oom_count;
    if (n != oom_seen) {
      // one text per caps and size: repeats count up (xN) instead of filling the log; caps first, the screen cuts
      Log_info("Event log: out of memory, %lu allocations failed, last %lu bytes caps 0x%lx",
               (unsigned long)(n - oom_seen), (unsigned long)oom_size, (unsigned long)oom_caps);
      evlog_add("out of memory: caps 0x%lx, %lu bytes failed", (unsigned long)oom_caps, (unsigned long)oom_size);
      oom_seen = n;
    }
    write_out(false);
  }
}

void evlog_begin(void) {
  if (mu) return;
  uint8_t *data = alloc(EVLOG_DATA_BYTES);
  snap = alloc(EVLOG_BYTES);
  SemaphoreHandle_t m = xSemaphoreCreateMutex(), w = xSemaphoreCreateMutex();
  if (!data || !snap || !m || !w) {
    Log_error("Event log: out of memory, off");
    return;
  }
  evlog_ring_init(&ring, data);
  File f = LittleFS.open(EVLOG_FILE, "r");
  size_t n = f ? f.read(snap, EVLOG_BYTES) : 0;
  if (f) f.close();
  bool loaded = n && evlog_ring_load(&ring, snap, n);
  if (n && !loaded) Log_error("Event log: %s unreadable (%u bytes), started empty", EVLOG_FILE, (unsigned)n);
  esp_reset_reason_t why = esp_reset_reason();
  bool crashed = why == ESP_RST_PANIC || why == ESP_RST_INT_WDT || why == ESP_RST_TASK_WDT || why == ESP_RST_WDT;
  // the last boot's events not yet in the file, and a crash's record, from RTC memory
  static evlog_pending_t pending[EVLOG_CRASH_PENDING];
  char crash[EVLOG_TEXT_MAX + 1];
  uint16_t pboot = 0;
  int np = evlog_crash_take(crashed, pending, EVLOG_CRASH_PENDING, crash, sizeof(crash), &pboot);
  uint32_t last_ms = 0;
  for (int i = 0; i < np; i++) {
    evlog_ring_add_boot(&ring, pending[i].boot, pending[i].ms, pending[i].wall, pending[i].text);
    last_ms = pending[i].ms;
  }
  if (crash[0]) {
    evlog_ring_add_boot(&ring, pboot ? pboot : (uint16_t)ring.h.boot, last_ms, 0, crash);
    Log_error("Event log: %s", crash);
  }
  if (pboot && (uint16_t)(pboot - (uint16_t)ring.h.boot) < 0x8000u) ring.h.boot = pboot; // a boot that wrote nothing
  evlog_ring_new_boot(&ring);
  evlog_crash_set_boot((uint16_t)ring.h.boot);
  wmu = w;
  mu = m;
  if (why != ESP_RST_DEEPSLEEP) evlog_add("boot: %s (%d)", reset_str(why), (int)why); // not one line per wake
  if (n && !loaded) evlog_add("event log: the saved log was damaged and dropped");
  heap_caps_register_failed_alloc_callback(on_alloc_failed);
  evlog_crash_begin();
  if (xTaskCreatePinnedToCore(flusher, "evlog", 4096, nullptr, 1, &task, 1) != pdPASS) {
    Log_error("Event log: no flush task, written only at restarts");
  }
  Log_info("Event log: %d events, boot #%u", evlog_ring_count(&ring), (unsigned)(uint16_t)ring.h.boot);
}

void evlog_add(const char *fmt, ...) {
  if (!mu) return;
  char text[EVLOG_TEXT_MAX + 1];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(text, sizeof(text), fmt, ap);
  va_end(ap);
  uint32_t ms = millis();
  int64_t wall = (int64_t)time(nullptr);
  xSemaphoreTake(mu, portMAX_DELAY);
  evlog_ring_add(&ring, ms, wall, text);
  evlog_crash_pending((uint16_t)ring.h.boot, ms, wall > 0 && wall <= 0xFFFFFFFFll ? (uint32_t)wall : 0u, text);
  xSemaphoreGive(mu);
  if (task) xTaskNotifyGive(task);
}

void evlog_flush(bool last) {
  if (!mu) return;
  write_out(true);
  if (last) closed = true;
}

// A copy of the ring (`buf` of EVLOG_BYTES + EVLOG_DATA_BYTES): printing never holds the log's lock
static bool copy(evlog_ring_t *out, uint8_t *buf) {
  if (!mu) return false;
  xSemaphoreTake(mu, portMAX_DELAY);
  evlog_ring_save(&ring, buf, EVLOG_BYTES);
  xSemaphoreGive(mu);
  evlog_ring_init(out, buf + EVLOG_BYTES);
  return evlog_ring_load(out, buf, EVLOG_BYTES);
}

bool evlog_serial_command(const char *line) {
  bool all = strcmp(line, "TAPP EVLOG") == 0, clear = strcmp(line, "TAPP EVLOG CLEAR") == 0;
  if (!all && !clear) return false;
  if (clear) {
    if (mu) {
      xSemaphoreTake(mu, portMAX_DELAY);
      evlog_ring_clear(&ring);
      xSemaphoreGive(mu);
      write_out(true);
    }
    serial_out_printf("TAPP EVLOG CLEARED\n");
    return true;
  }
  uint8_t *buf = alloc(EVLOG_BYTES + EVLOG_DATA_BYTES);
  evlog_ring_t c;
  if (buf && copy(&c, buf)) {
    char l[EVLOG_LINE_MAX];
    int n = evlog_ring_count(&c);
    for (int i = 0; i < n; i++) {
      // paced, no flush(): HWCDC's flush() and a full ring drop bytes (serial_out.h); a host gone ends the answer
      if (evlog_ring_line(&c, i, l, sizeof(l)) >= 0 && !serial_out_printf("TAPP EVLOG %s\n", l)) break;
    }
  } else {
    serial_out_printf("TAPP EVLOG (no log: %s)\n", mu ? "out of memory" : "off");
  }
  free(buf);
  serial_out_printf("TAPP EVLOG END\n");
  return true;
}

int evlog_tail(char *buf, size_t len, uint32_t n) {
  if (!buf || !len) return APP_EINVAL;
  uint8_t *tmp = alloc(EVLOG_BYTES + EVLOG_DATA_BYTES);
  evlog_ring_t c;
  int rc = tmp && copy(&c, tmp) ? evlog_ring_tail(&c, n, buf, len) : APP_ENOMEM;
  if (rc < 0) buf[0] = '\0';
  free(tmp);
  return rc;
}

#endif // BOARD_TRMNL_X
