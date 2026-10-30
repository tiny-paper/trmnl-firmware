#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ble_pair.h"
#include "hid_report_map.h"

// One link to a Bluetooth LE keyboard, from connected to typing (HID over GATT, HOGP), as pure logic: the caller
// (src/ble_keyboard.cpp) feeds NimBLE's results and carries out the returned action. Security comes first: no
// GATT request is made before the link is encrypted (a keyboard refuses its HID service on a plain link, and
// IDF's esp_hid host reads it too early). Then the HID service, its characteristics and descriptors, the report
// map, the report references, protocol mode and the notifications of the keyboard's input report. A keyboard
// whose map has a keyboard input report is used in report protocol; one without is set to boot protocol.

#define HOGP_MAX_CHRS    24
#define HOGP_SECURE_MS   15000 // connected to encrypted, nothing to type (bonded, or just-works)
#define HOGP_PASSKEY_MS  40000 // passkey on screen to encrypted (SMP's own timeout is 30 s)
#define HOGP_DISCOVER_MS 10000 // encrypted to subscribed

#define HOGP_UUID_HID_SERVICE  0x1812
#define HOGP_UUID_REPORT_MAP   0x2A4B
#define HOGP_UUID_REPORT       0x2A4D
#define HOGP_UUID_PROTOCOL     0x2A4E
#define HOGP_UUID_BOOT_KBD_IN  0x2A22
#define HOGP_UUID_CCCD         0x2902
#define HOGP_UUID_REPORT_REF   0x2908

// NimBLE's status numbers (host/ble_hs.h; src/ble_keyboard.cpp checks them with static_assert)
#define HOGP_HS_ETIMEOUT   13
#define HOGP_HS_ATT_BASE   0x100
#define HOGP_HS_HCI_BASE   0x200
#define HOGP_HS_SM_US_BASE 0x400
#define HOGP_HS_SM_PEER_BASE 0x500

typedef enum {
  HOGP_IDLE,
  HOGP_SECURING,
  HOGP_PASSKEY,
  HOGP_FIND_SERVICE,
  HOGP_FIND_CHRS,
  HOGP_FIND_DSCS,
  HOGP_READ_MAP,
  HOGP_READ_REFS,
  HOGP_SET_PROTOCOL,
  HOGP_SUBSCRIBE,
  HOGP_READY,
  HOGP_FAILED,
} hogp_state_t;

typedef enum {
  HOGP_DO_NONE,
  HOGP_DO_SECURE, // ble_gap_security_initiate
  HOGP_DO_FIND_SERVICE, // discover the primary service 0x1812
  HOGP_DO_FIND_CHRS, // all characteristics in handle..end
  HOGP_DO_FIND_DSCS, // all descriptors in handle..end
  HOGP_DO_READ, // `handle`
  HOGP_DO_READ_LONG, // `handle`, the report map
  HOGP_DO_WRITE_NO_RSP, // value[0..len) to `handle`, then call hogp_written(.., 0) at once
  HOGP_DO_WRITE, // value[0..len) to `handle`, hogp_written on the response
  HOGP_DO_READY, // typing: ble_pair_connected
  HOGP_DO_FAIL, // `why`: drop the link, ble_pair_failed
} hogp_do_t;

typedef struct {
  hogp_do_t what;
  uint16_t handle, end;
  uint8_t value[2];
  uint8_t len;
  ble_fail_t why;
} hogp_act_t;

typedef struct {
  uint16_t uuid, val, cccd, ref;
  uint8_t report_id, report_type; // from the report reference (type 1 input)
} hogp_chr_t;

typedef struct {
  hogp_state_t state;
  uint32_t deadline;
  uint16_t svc_start, svc_end;
  hogp_chr_t chr[HOGP_MAX_CHRS];
  int chrs;
  int next_ref; // index into chr while reading report references
  uint8_t map[HID_MAP_MAX_LEN];
  uint16_t map_len;
  bool map_too_long;
  hid_map_kbd_t kbd;
  bool boot; // boot protocol
  int in; // the subscribed characteristic
} hogp_t;

// Connected (the link is up, not yet encrypted)
hogp_act_t hogp_start(hogp_t *h, uint32_t now_ms);
// The keyboard asks for a passkey to be shown: longer to wait
void hogp_passkey(hogp_t *h, uint32_t now_ms);
// NimBLE's encryption change; status 0 encrypted
hogp_act_t hogp_encrypted(hogp_t *h, int status, uint32_t now_ms);

void hogp_service(hogp_t *h, uint16_t uuid16, uint16_t start, uint16_t end);
hogp_act_t hogp_services_done(hogp_t *h, int status);
void hogp_chr(hogp_t *h, uint16_t uuid16, uint16_t val_handle);
hogp_act_t hogp_chrs_done(hogp_t *h, int status);
void hogp_dsc(hogp_t *h, uint16_t handle, uint16_t uuid16);
hogp_act_t hogp_dscs_done(hogp_t *h, int status);
void hogp_read_data(hogp_t *h, const uint8_t *data, size_t len);
hogp_act_t hogp_read_done(hogp_t *h, int status);
hogp_act_t hogp_written(hogp_t *h, int status);

// Deadlines: the action is HOGP_DO_FAIL when one passed
hogp_act_t hogp_tick(hogp_t *h, uint32_t now_ms);

// A notification: true with `boot` filled when it is the keyboard's input report
bool hogp_report(const hogp_t *h, uint16_t attr_handle, const uint8_t *data, size_t len, uint8_t boot[8]);

// Why an encryption change failed
ble_fail_t hogp_enc_fail(int status, bool passkey_shown);
