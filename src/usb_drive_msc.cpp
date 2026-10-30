// TinyUSB device: descriptors and MSC callbacks after TinyUSB's msc_disk example (MIT) and
// esp_tinyusb's defaults (Apache-2.0: Espressif's VID, PID 0x4002 for MSC only)
#ifdef BOARD_TRMNL_X

#include "usb_drive_msc.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <string.h>
#include <tusb.h>

#define MSC_TASK_STACK 4096
#define SECTOR 512

static uint8_t *image;
static uint32_t image_sectors;
static drive_session_t *session;
static volatile bool ejected;
static volatile bool running;
static volatile uint32_t suspended_since; // 0: not suspended
static TaskHandle_t task;
static SemaphoreHandle_t task_done;

// ---- descriptors ----

static const tusb_desc_device_t device_desc = {
    sizeof(tusb_desc_device_t), TUSB_DESC_DEVICE, 0x0200, 0x00, 0x00, 0x00, CFG_TUD_ENDPOINT0_SIZE,
    0x303A, // Espressif
    0x4002, // esp_tinyusb's PID for MSC only
    0x0100, 1, 2, 3, 1};

enum { ITF_MSC, ITF_COUNT };
#define EP_MSC_OUT 0x01
#define EP_MSC_IN 0x81
#define CONFIG_LEN (TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN)

static const uint8_t config_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_COUNT, 0, CONFIG_LEN, 0x00, 100),
    TUD_MSC_DESCRIPTOR(ITF_MSC, 4, EP_MSC_OUT, EP_MSC_IN, 64),
};

static const char *const strings[] = {"", "TRMNL", "TRMNL X writer", "000001", "Writer drive"};

extern "C" uint8_t const *tud_descriptor_device_cb(void) { return (uint8_t const *)&device_desc; }

extern "C" uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
  (void)index;
  return config_desc;
}

extern "C" uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void)langid;
  static uint16_t out[33];
  uint8_t n;
  if (index == 0) {
    out[1] = 0x0409; // English
    n = 1;
  } else {
    if (index >= sizeof(strings) / sizeof(strings[0])) return nullptr;
    const char *s = strings[index];
    n = (uint8_t)strlen(s);
    if (n > 32) n = 32;
    for (uint8_t i = 0; i < n; i++) out[1 + i] = (uint8_t)s[i];
  }
  out[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * n + 2));
  return out;
}

// ---- bus ----

extern "C" void tud_suspend_cb(bool remote_wakeup_en) {
  (void)remote_wakeup_en;
  uint32_t now = millis();
  suspended_since = now ? now : 1;
}

extern "C" void tud_resume_cb(void) { suspended_since = 0; }
extern "C" void tud_mount_cb(void) {
  suspended_since = 0;
  if (session) session->mounted = true;
}

// ---- mass storage ----

extern "C" void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16],
                                   uint8_t product_rev[4]) {
  (void)lun;
  memcpy(vendor_id, "TRMNL   ", 8);
  memcpy(product_id, "X writer drive  ", 16);
  memcpy(product_rev, "1.0 ", 4);
}

extern "C" bool tud_msc_test_unit_ready_cb(uint8_t lun) {
  (void)lun;
  if (ejected || !image) {
    tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3a, 0x00); // medium not present
    return false;
  }
  return true;
}

extern "C" void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size) {
  (void)lun;
  *block_count = image_sectors;
  *block_size = SECTOR;
}

extern "C" bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject) {
  (void)lun;
  (void)power_condition;
  if (load_eject && !start) ejected = true;
  return true;
}

static bool in_image(uint32_t lba, uint32_t offset, uint32_t len) {
  uint64_t end = (uint64_t)lba * SECTOR + offset + len;
  return image && lba < image_sectors && end <= (uint64_t)image_sectors * SECTOR;
}

extern "C" int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
  if (ejected || !in_image(lba, offset, bufsize)) {
    tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x21, 0x00); // LBA out of range
    return -1;
  }
  memcpy(buffer, image + (size_t)lba * SECTOR + offset, bufsize);
  return (int32_t)bufsize;
}

extern "C" bool tud_msc_is_writable_cb(uint8_t lun) {
  (void)lun;
  return !ejected;
}

extern "C" int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer,
                                      uint32_t bufsize) {
  if (ejected || !in_image(lba, offset, bufsize)) {
    tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x21, 0x00);
    return -1;
  }
  memcpy(image + (size_t)lba * SECTOR + offset, buffer, bufsize);
  if (session) drive_session_wrote(session, millis());
  return (int32_t)bufsize;
}

extern "C" int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize) {
  (void)buffer;
  (void)bufsize;
  switch (scsi_cmd[0]) {
  case 0x35: // SYNCHRONIZE CACHE (10): the image is the medium, nothing to flush
    if (session) drive_session_synced(session);
    return 0;
  case 0x1e: // PREVENT ALLOW MEDIUM REMOVAL
    return 0;
  default:
    tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00); // invalid command
    return -1;
  }
}

// ---- the stack ----

static void msc_task(void *arg) {
  (void)arg;
  while (running) tud_task_ext(20, false);
  xSemaphoreGive(task_done);
  vTaskDelete(nullptr);
}

bool usb_drive_msc_start(uint8_t *img, uint32_t sectors, drive_session_t *s) {
  if (task) return false;
  image = img;
  image_sectors = sectors;
  session = s;
  ejected = false;
  suspended_since = 0;
  if (!task_done) task_done = xSemaphoreCreateBinary();
  tusb_rhport_init_t init = {TUSB_ROLE_DEVICE, TUSB_SPEED_AUTO};
  if (!task_done || !tusb_init(0, &init)) {
    image = nullptr;
    return false;
  }
  running = true;
  if (xTaskCreatePinnedToCore(msc_task, "usb_drive", MSC_TASK_STACK, nullptr, 5, &task, 0) != pdPASS) {
    running = false;
    task = nullptr;
    tusb_deinit(0);
    image = nullptr;
    return false;
  }
  return true;
}

void usb_drive_msc_stop(void) {
  if (!task) return;
  tud_disconnect();
  running = false;
  xSemaphoreTake(task_done, portMAX_DELAY);
  task = nullptr;
  tusb_deinit(0);
  image = nullptr;
  session = nullptr;
}

bool usb_drive_msc_mounted(void) { return task && tud_mounted(); }
bool usb_drive_msc_ejected(void) { return ejected; }

uint32_t usb_drive_msc_suspended_ms(void) {
  uint32_t since = suspended_since;
  return since ? millis() - since : 0;
}

#endif // BOARD_TRMNL_X
