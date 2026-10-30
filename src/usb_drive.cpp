// The USB drive for the running app (usb_drive.h): exports its writer files to a FAT image in
// PSRAM, gives the USB PHY to TinyUSB's mass storage, and on the end takes the computer's changes
// back by the owner's rules (drive_sync.h, drive_session.h). The computer never writes LittleFS.
#ifdef BOARD_TRMNL_X

#include "usb_drive.h"

#include <Arduino.h>
#include <drive_session.h>
#include <drive_sync.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <power.h>
#include <string.h>
#include <time.h>
#include <trmnl_log.h>

#include "app_host.h"
#include "drive_littlefs.h"
#include "usb_drive_msc.h"
#include "usb_port_role.h"

#define DRIVE_TIMEOUT_MS 60000
#define DRIVE_IMG_MAX (6u * 1024 * 1024)
#define DRIVE_IMG_HEADROOM (512u * 1024) // PSRAM left for the firmware
#define DRIVE_SUSPEND_MS 3000 // a bus suspended this long: the computer is gone or asleep
#define DRIVE_VBUS_POLL_MS 500

enum { OFF, SHOWN, ENDING };

static struct {
  volatile int state;
  uint8_t *img;
  size_t img_size;
  drive_manifest_entry_t *entries;
  drive_manifest_t manifest;
  drive_session_t session;
  uint32_t started, timeout, vbus_checked;
  app_drive_result_t result;
  drive_fs_t fs;
} d;

static SemaphoreHandle_t lock;

static void take_lock(void) {
  if (!lock) lock = xSemaphoreCreateMutex();
  xSemaphoreTake(lock, portMAX_DELAY);
}

static void give_lock(void) { xSemaphoreGive(lock); }

static void release(void) {
  heap_caps_free(d.img);
  heap_caps_free(d.entries);
  d.img = nullptr;
  d.entries = nullptr;
}

static int app_error(int drive_rc) {
  switch (drive_rc) {
  case DRIVE_EFULL:
  case DRIVE_ETOOMANY:
    return APP_ENOSPC;
  case DRIVE_ENOMEM:
    return APP_ENOMEM;
  case DRIVE_EINVAL:
    return APP_EINVAL;
  default:
    return APP_EIO;
  }
}

static uint32_t fat_now(void) {
  time_t now = time(nullptr);
  struct tm t;
  if (now < 1700000000 || !localtime_r(&now, &t)) return drive_fat_time(2026, 1, 1, 0, 0, 0);
  return drive_fat_time(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
}

int usb_drive_start(uint32_t connect_timeout_ms) {
  take_lock();
  int rc = 0;
  if (d.state != OFF) rc = APP_EBUSY;
  if (rc == 0) {
    drive_littlefs(&d.fs, app_host.dir);
    int r = drive_recover(&d.fs);
    if (r < 0) rc = app_error(r);
  }
  if (rc == 0) {
    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    size_t size = largest > DRIVE_IMG_HEADROOM ? largest - DRIVE_IMG_HEADROOM : 0;
    if (size > DRIVE_IMG_MAX) size = DRIVE_IMG_MAX;
    size &= ~(size_t)(64 * 1024 - 1);
    d.entries = (drive_manifest_entry_t *)heap_caps_malloc(DRIVE_MAX_FILES * sizeof(drive_manifest_entry_t),
                                                           MALLOC_CAP_SPIRAM);
    d.img = size >= DRIVE_FAT_MIN_BYTES ? (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_SPIRAM) : nullptr;
    d.img_size = size;
    if (!d.img || !d.entries) rc = APP_ENOMEM;
  }
  memset(&d.result, 0, sizeof(d.result));
  if (rc == 0) {
    d.manifest = {d.entries, 0};
    drive_export_result_t ex;
    int r = drive_export(&d.fs, d.img, d.img_size, &d.manifest, fat_now(), &ex);
    if (r < 0) {
      rc = app_error(r);
    } else {
      d.result.shown = (uint16_t)ex.shown;
      d.result.not_shown = (uint16_t)ex.not_shown;
    }
  }
  if (rc == 0) {
    drive_session_reset(&d.session);
    if (!usb_port_take(USB_ROLE_DRIVE)) {
      rc = usb_port_role() == USB_ROLE_SERIAL ? APP_EIO : APP_EBUSY; // EBUSY: a USB keyboard has the port
    } else if (!usb_drive_msc_start(d.img, (uint32_t)(d.img_size / DRIVE_FAT_SECTOR), &d.session)) {
      usb_port_give(USB_ROLE_DRIVE);
      rc = APP_EIO;
    }
  }
  if (rc == 0) {
    d.started = millis();
    d.vbus_checked = d.started;
    d.timeout = connect_timeout_ms ? connect_timeout_ms : DRIVE_TIMEOUT_MS;
    d.state = SHOWN;
    Log_info("USB drive: %s shows %u files (%u not shown), %u KB", app_host.name, d.result.shown,
             d.result.not_shown, (unsigned)(d.img_size / 1024));
  } else {
    release();
    Log_error("USB drive: %s not shown (%d)", app_host.name, rc);
  }
  give_lock();
  return rc;
}

// Ends a showing: the computer's side first (no callback runs after msc_stop), serial back,
// then the import or not. Under the lock.
static void end(drive_end_t why) {
  if (d.state != SHOWN) return;
  d.state = ENDING;
  usb_drive_msc_stop();
  usb_port_give(USB_ROLE_DRIVE);
  uint32_t now = millis();
  d.result.ended_by = (uint8_t)why;
  bool take = drive_session_take(&d.session, why, now);
  d.result.cut = d.session.writes && !take;
  if (take) {
    drive_import_result_t r;
    int rc = drive_import(&d.fs, d.img, d.img_size, &d.manifest, &r);
    d.result.result = rc < 0 ? (rc == DRIVE_EIMAGE ? APP_EIO : app_error(rc)) : 0;
    d.result.taken = rc == DRIVE_OK;
    d.result.added = (uint16_t)r.added;
    d.result.updated = (uint16_t)r.updated;
    d.result.conflicts = (uint16_t)r.conflicts;
    d.result.kept_deleted = (uint16_t)r.kept_deleted;
    d.result.bad_names = (uint16_t)r.bad_names;
    d.result.left_alone = (uint16_t)r.left_alone;
    Log_info("USB drive: ended (%d), import %d: %u added, %u updated, %u conflicts, %u bad names", why, rc,
             d.result.added, d.result.updated, d.result.conflicts, d.result.bad_names);
  } else {
    Log_info("USB drive: ended (%d), %s", why, d.result.cut ? "unplugged during a write: nothing taken" : "no changes");
  }
  release();
  d.state = OFF;
}

int usb_drive_state(void) {
  if (d.state != SHOWN) return APP_DRIVE_OFF;
  return usb_drive_msc_mounted() ? APP_DRIVE_IN_USE : APP_DRIVE_SHOWN;
}

int usb_drive_stop(app_drive_result_t *res) {
  take_lock();
  end(DRIVE_END_STOPPED);
  if (res) *res = d.result;
  int rc = d.result.result;
  give_lock();
  return rc;
}

void usb_drive_service(void) {
  if (d.state != SHOWN) return;
  uint32_t now = millis();
  drive_end_t why = DRIVE_END_NONE;
  if (usb_drive_msc_ejected()) {
    why = DRIVE_END_EJECTED;
  } else if (d.session.mounted) {
    if (usb_drive_msc_suspended_ms() >= DRIVE_SUSPEND_MS) why = DRIVE_END_UNPLUGGED;
    if (now - d.vbus_checked >= DRIVE_VBUS_POLL_MS) {
      d.vbus_checked = now;
      if (power().usbStatus() == UsbStatus::DISCONNECTED) why = DRIVE_END_UNPLUGGED;
    }
  } else if (now - d.started >= d.timeout) {
    why = DRIVE_END_TIMEOUT;
  }
  if (why == DRIVE_END_NONE) return;
  take_lock();
  end(why);
  give_lock();
}

void usb_drive_end_for_exit(void) {
  take_lock();
  end(DRIVE_END_STOPPED);
  give_lock();
}

bool usb_drive_busy(void) { return d.state != OFF; }
bool usb_drive_importing(void) { return d.state == ENDING; }

void usb_drive_recover(const char *app_dir) {
  drive_fs_t fs;
  drive_littlefs(&fs, app_dir);
  int rc = drive_recover(&fs);
  if (rc < 0) Log_error("USB drive: an interrupted import in %s could not be finished (%d)", app_dir, rc);
}

#endif // BOARD_TRMNL_X
