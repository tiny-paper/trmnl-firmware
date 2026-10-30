// Receives apps over the USB serial port into /apps (apps.h, docs/apps.md "Copy an app to the X"): the
// protocol of app_upload.h, host side apps/upload.py. Only /apps/<name>.elf and a temporary file are
// written; the rest of the file system (cached screens, the modem file) is left alone.
#ifdef BOARD_TRMNL_X

#include <Arduino.h>
#include <LittleFS.h>
#include <app_install.h>
#include <app_upload.h>
#include <esp_heap_caps.h>
#include <trmnl_crc32.h>
#include <apps.h>
#include <trmnl_log.h>

#include <algorithm>

#include "webshare.h" // webshare_fs_root

#define RECEIVE_IDLE_MS (2u * 60u * 1000u) // no upload started for this long: back to the menu

static void reply(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void reply(const char *fmt, ...) {
  char line[APP_UPLOAD_LINE_MAX];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(line, sizeof(line), fmt, ap);
  va_end(ap);
  Serial.printf("TAPP %s\n", line);
  Serial.flush();
}

// A line from the port into `line`; false on a cancel or after `idle_ms` without a byte
static bool read_line(char *line, size_t cap, uint32_t idle_ms, bool (*cancel)(void)) {
  size_t len = 0;
  bool too_long = false;
  uint32_t last = millis();
  while (true) {
    int b = Serial.read();
    if (b < 0) {
      if (cancel && cancel()) return false;
      if (millis() - last >= idle_ms) return false;
      delay(5);
      continue;
    }
    last = millis();
    if (b == '\r') continue;
    if (b == '\n') {
      if (too_long) {
        len = 0;
        too_long = false;
        reply("ERR %s", app_upload_result_str(APP_UPLOAD_BAD_HEADER));
        continue;
      }
      line[len] = '\0';
      return true;
    }
    if (len + 1 < cap) line[len++] = (char)b;
    else too_long = true;
  }
}

// The complete upload in APP_UPLOAD_TMP becomes the app the same way as an Install via Wi-Fi: app_install_commit
static app_upload_result_t install(const app_upload_header_t &h, char *reason, size_t reason_len) {
  uint8_t *buf = (uint8_t *)heap_caps_malloc(h.size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  size_t free_mem = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  app_install_result_t rc = app_install_commit(webshare_fs_root(), APP_UPLOAD_TMP + 1, h.path + 1, h.size, buf,
                                               buf ? h.size : 0, free_mem, reason, reason_len);
  heap_caps_free(buf);
  if (rc == APP_INSTALL_OK) return APP_UPLOAD_OK;
  return rc == APP_INSTALL_REFUSED ? APP_UPLOAD_REFUSED : rc == APP_INSTALL_NO_SPACE ? APP_UPLOAD_NO_SPACE
                                                                                      : APP_UPLOAD_WRITE_FAILED;
}

// The data of one upload into APP_UPLOAD_TMP, then installed. `reason`: why the ELF check refused it
static app_upload_result_t receive_data(const app_upload_header_t &h, char *reason, size_t reason_len) {
  reason[0] = '\0';
  LittleFS.remove(APP_UPLOAD_TMP);
  File f = LittleFS.open(APP_UPLOAD_TMP, "w");
  if (!f) return APP_UPLOAD_WRITE_FAILED;
  reply("READY");
  static uint8_t buf[APP_UPLOAD_BLOCK];
  uint32_t got = 0, crc = 0, in_block = 0, last = millis();
  bool write_failed = false;
  while (got < h.size && millis() - last < APP_UPLOAD_IDLE_MS) {
    int avail = Serial.available();
    if (avail <= 0) {
      delay(1);
      continue;
    }
    size_t want = std::min<size_t>({(size_t)avail, h.size - got, APP_UPLOAD_BLOCK - in_block});
    size_t n = Serial.read(buf, want);
    if (!n) continue;
    last = millis();
    if (!write_failed && f.write(buf, n) != n) write_failed = true; // keeps reading: the host gets its reply
    crc = trmnl_crc32(crc, buf, n);
    got += n;
    in_block += n;
    if (in_block == APP_UPLOAD_BLOCK && got < h.size) {
      reply("ACK %lu", (unsigned long)got);
      in_block = 0;
    }
  }
  f.close();
  app_upload_result_t r = write_failed ? APP_UPLOAD_WRITE_FAILED : app_upload_check(&h, got, crc);
  if (r == APP_UPLOAD_OK) r = install(h, reason, reason_len);
  if (r != APP_UPLOAD_OK) {
    LittleFS.remove(APP_UPLOAD_TMP);
    Log_error("Apps: upload of %s failed: %s %s (%lu of %lu bytes)", h.path, app_upload_result_str(r), reason,
              (unsigned long)got, (unsigned long)h.size);
  }
  return r;
}

int apps_receive(bool (*cancel)(void)) {
  int received = 0;
  Serial.begin(115200);
  if (!LittleFS.exists(APPS_DIR)) LittleFS.mkdir(APPS_DIR);
  LittleFS.remove(APP_UPLOAD_TMP); // left by a restart during an upload
  Log_info("Apps: waiting for an upload on the USB serial port");
  char line[APP_UPLOAD_LINE_MAX];
  while (read_line(line, sizeof(line), RECEIVE_IDLE_MS, cancel)) {
    if (!line[0]) continue;
    app_upload_header_t h;
    uint64_t total = LittleFS.totalBytes(), used = LittleFS.usedBytes();
    app_upload_result_t r = app_upload_parse(line, used < total ? total - used : 0, &h);
    char reason[64] = "";
    if (r == APP_UPLOAD_OK) r = receive_data(h, reason, sizeof(reason));
    if (r != APP_UPLOAD_OK) {
      Log_info("Apps: upload refused: %s%s%s", app_upload_result_str(r), reason[0] ? ": " : "", reason);
      reply("ERR %s%s%s", app_upload_result_str(r), reason[0] ? ": " : "", reason);
      continue;
    }
    received++;
    Log_info("Apps: received %s, %lu bytes", h.path, (unsigned long)h.size);
    reply("OK %s", h.path + sizeof(APPS_DIR));
  }
  Log_info("Apps: upload mode ended, %d received", received);
  return received;
}

#endif // BOARD_TRMNL_X
