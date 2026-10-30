// The X's event log: the ring of evlog_ring.h in RAM, written to /evlog.bin by its own task (evlog.h).
#ifdef BOARD_TRMNL_X

#include "evlog.h"

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

static void flusher(void *arg) {
  (void)arg;
  while (true) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(EVLOG_TASK_POLL_MS));
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
  evlog_ring_new_boot(&ring);
  wmu = w;
  mu = m;
  esp_reset_reason_t why = esp_reset_reason();
  if (why != ESP_RST_DEEPSLEEP) evlog_add("boot: %s (%d)", reset_str(why), (int)why); // not one line per wake
  if (n && !loaded) evlog_add("event log: the saved log was damaged and dropped");
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
  xSemaphoreTake(mu, portMAX_DELAY);
  evlog_ring_add(&ring, millis(), (int64_t)time(nullptr), text);
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
    Serial.printf("TAPP EVLOG CLEARED\n");
    return true;
  }
  uint8_t *buf = alloc(EVLOG_BYTES + EVLOG_DATA_BYTES);
  evlog_ring_t c;
  if (buf && copy(&c, buf)) {
    char l[EVLOG_LINE_MAX];
    int n = evlog_ring_count(&c);
    for (int i = 0; i < n; i++) {
      if (evlog_ring_line(&c, i, l, sizeof(l)) >= 0) Serial.printf("TAPP EVLOG %s\n", l);
    }
  } else {
    Serial.printf("TAPP EVLOG (no log: %s)\n", mu ? "out of memory" : "off");
  }
  free(buf);
  Serial.printf("TAPP EVLOG END\n");
  Serial.flush();
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
