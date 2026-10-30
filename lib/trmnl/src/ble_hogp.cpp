#include "ble_hogp.h"

#include <string.h>

static hogp_act_t act(hogp_do_t what, uint16_t handle = 0, uint16_t end = 0) {
  hogp_act_t a = {};
  a.what = what;
  a.handle = handle;
  a.end = end;
  return a;
}

static hogp_act_t fail(hogp_t *h, ble_fail_t why) {
  h->state = HOGP_FAILED;
  hogp_act_t a = act(HOGP_DO_FAIL);
  a.why = why;
  return a;
}

static hogp_act_t write(hogp_do_t what, uint16_t handle, uint8_t v0, uint8_t v1, uint8_t len) {
  hogp_act_t a = act(what, handle);
  a.value[0] = v0;
  a.value[1] = v1;
  a.len = len;
  return a;
}

// A failed GATT step after encryption: refused for security, a timeout, or not what a keyboard has
static ble_fail_t gatt_fail(int status) {
  if (status == HOGP_HS_ETIMEOUT) return BLE_FAIL_TIMEOUT;
  int att = status - HOGP_HS_ATT_BASE;
  if (att == 0x05 || att == 0x0F) return BLE_FAIL_AUTH; // insufficient authentication / encryption
  return BLE_FAIL_NOT_HID;
}

ble_fail_t hogp_enc_fail(int status, bool passkey_shown) {
  if (status == HOGP_HS_HCI_BASE + 0x06) return BLE_FAIL_KEY_MISSING; // PIN or key missing: it lost our bond
  if (status == HOGP_HS_ETIMEOUT) return passkey_shown ? BLE_FAIL_PASSKEY_TIMEOUT : BLE_FAIL_TIMEOUT;
  if (status == HOGP_HS_HCI_BASE + 0x05) return BLE_FAIL_AUTH; // authentication failure
  if (status >= HOGP_HS_SM_US_BASE && status < HOGP_HS_SM_PEER_BASE + 0x100) {
    return passkey_shown && (status & 0xFF) == 0x01 ? BLE_FAIL_PASSKEY_TIMEOUT : BLE_FAIL_AUTH;
  }
  return BLE_FAIL_TIMEOUT; // the link went (ENOTCONN and the like)
}

hogp_act_t hogp_start(hogp_t *h, uint32_t now) { return hogp_start_cached(h, now, nullptr); }

hogp_act_t hogp_start_cached(hogp_t *h, uint32_t now, const hogp_cache_t *c) {
  memset(h, 0, sizeof(*h));
  h->in = -1;
  h->state = HOGP_SECURING;
  h->deadline = now + HOGP_SECURE_MS;
  if (c && c->version == HOGP_CACHE_VERSION && c->chrs <= HOGP_MAX_CHRS) {
    h->from_cache = true;
    h->svc_start = c->svc_start;
    h->svc_end = c->svc_end;
    h->chrs = c->chrs;
    memcpy(h->chr, c->chr, sizeof(h->chr));
    h->kbd = c->kbd;
  }
  return act(HOGP_DO_SECURE);
}

bool hogp_cache_valid(const hogp_cache_t *c, const uint8_t addr[7]) {
  if (!c || !addr || c->version != HOGP_CACHE_VERSION || memcmp(c->addr, addr, sizeof(c->addr)) != 0) return false;
  if (c->chrs == 0 || c->chrs > HOGP_MAX_CHRS || c->svc_start == 0 || c->svc_end < c->svc_start) return false;
  for (int i = 0; i < c->chrs; i++) {
    if (c->chr[i].val <= c->svc_start || c->chr[i].val > c->svc_end) return false;
  }
  return true;
}

bool hogp_cache_fill(const hogp_t *h, const uint8_t addr[7], hogp_cache_t *c) {
  if (!h || !addr || !c || h->state != HOGP_READY) return false;
  memset(c, 0, sizeof(*c));
  c->version = HOGP_CACHE_VERSION;
  memcpy(c->addr, addr, sizeof(c->addr));
  c->svc_start = h->svc_start;
  c->svc_end = h->svc_end;
  c->chrs = (uint8_t)h->chrs;
  memcpy(c->chr, h->chr, sizeof(c->chr));
  c->kbd = h->kbd;
  return true;
}

// The kept handles are wrong: forget them and discover on this link
static hogp_act_t rediscover(hogp_t *h, uint32_t deadline) {
  h->from_cache = false;
  h->cache_stale = true;
  h->svc_start = h->svc_end = 0;
  h->chrs = 0;
  h->in = -1;
  memset(&h->kbd, 0, sizeof(h->kbd));
  h->state = HOGP_FIND_SERVICE;
  h->deadline = deadline;
  return act(HOGP_DO_FIND_SERVICE);
}

static hogp_act_t choose(hogp_t *h);

hogp_act_t hogp_secure_started(hogp_t *h, int rc, bool encrypted, uint32_t now) {
  if (h->state != HOGP_SECURING) return act(HOGP_DO_NONE);
  if (encrypted) return hogp_encrypted(h, 0, now);
  if (rc == 0 || rc == HOGP_HS_EALREADY) return act(HOGP_DO_NONE); // the encryption change follows
  return fail(h, hogp_enc_fail(rc, false));
}

void hogp_passkey(hogp_t *h, uint32_t now) {
  if (h->state != HOGP_SECURING && h->state != HOGP_PASSKEY) return;
  h->state = HOGP_PASSKEY;
  h->deadline = now + HOGP_PASSKEY_MS;
}

hogp_act_t hogp_encrypted(hogp_t *h, int status, uint32_t now) {
  if (h->state != HOGP_SECURING && h->state != HOGP_PASSKEY) return act(HOGP_DO_NONE);
  if (status != 0) return fail(h, hogp_enc_fail(status, h->state == HOGP_PASSKEY));
  h->state = HOGP_FIND_SERVICE;
  h->deadline = now + HOGP_DISCOVER_MS;
  if (h->from_cache) {
    hogp_act_t a = choose(h); // protocol mode, then subscribe
    if (a.what != HOGP_DO_FAIL) return a;
    return rediscover(h, h->deadline); // the kept handles name no keyboard input
  }
  return act(HOGP_DO_FIND_SERVICE);
}

void hogp_service(hogp_t *h, uint16_t uuid16, uint16_t start, uint16_t end) {
  if (h->state != HOGP_FIND_SERVICE || uuid16 != HOGP_UUID_HID_SERVICE || h->svc_start) return;
  if (start == 0 || end < start) return;
  h->svc_start = start;
  h->svc_end = end;
}

hogp_act_t hogp_services_done(hogp_t *h, int status) {
  if (h->state != HOGP_FIND_SERVICE) return act(HOGP_DO_NONE);
  if (status != 0 && !h->svc_start) return fail(h, gatt_fail(status));
  if (!h->svc_start) return fail(h, BLE_FAIL_NOT_HID);
  h->state = HOGP_FIND_CHRS;
  return act(HOGP_DO_FIND_CHRS, h->svc_start, h->svc_end);
}

void hogp_chr(hogp_t *h, uint16_t uuid16, uint16_t val) {
  if (h->state != HOGP_FIND_CHRS || h->chrs == HOGP_MAX_CHRS) return;
  if (val <= h->svc_start || val > h->svc_end) return;
  hogp_chr_t *c = &h->chr[h->chrs++];
  memset(c, 0, sizeof(*c));
  c->uuid = uuid16; // 0 for a 128-bit one: kept, so its descriptors are not taken for the one before
  c->val = val;
}

static int find_chr(const hogp_t *h, uint16_t uuid) {
  for (int i = 0; i < h->chrs; i++) {
    if (h->chr[i].uuid == uuid) return i;
  }
  return -1;
}

hogp_act_t hogp_chrs_done(hogp_t *h, int status) {
  if (h->state != HOGP_FIND_CHRS) return act(HOGP_DO_NONE);
  if (status != 0 && h->chrs == 0) return fail(h, gatt_fail(status));
  bool any_input = find_chr(h, HOGP_UUID_REPORT) >= 0 || find_chr(h, HOGP_UUID_BOOT_KBD_IN) >= 0;
  if (find_chr(h, HOGP_UUID_REPORT_MAP) < 0 || !any_input) return fail(h, BLE_FAIL_NOT_HID);
  h->state = HOGP_FIND_DSCS;
  return act(HOGP_DO_FIND_DSCS, h->svc_start, h->svc_end);
}

void hogp_dsc(hogp_t *h, uint16_t handle, uint16_t uuid16) {
  if (h->state != HOGP_FIND_DSCS) return;
  if (uuid16 != HOGP_UUID_CCCD && uuid16 != HOGP_UUID_REPORT_REF) return;
  // the characteristic it follows: the one with the highest value handle below it
  int owner = -1;
  for (int i = 0; i < h->chrs; i++) {
    if (h->chr[i].val < handle && (owner < 0 || h->chr[i].val > h->chr[owner].val)) owner = i;
  }
  if (owner < 0) return;
  hogp_chr_t *c = &h->chr[owner];
  if (uuid16 == HOGP_UUID_CCCD && !c->cccd) c->cccd = handle;
  if (uuid16 == HOGP_UUID_REPORT_REF && !c->ref) c->ref = handle;
}

hogp_act_t hogp_dscs_done(hogp_t *h, int status) {
  if (h->state != HOGP_FIND_DSCS) return act(HOGP_DO_NONE);
  (void)status; // descriptors found so far are used; a missing CCCD is caught when choosing
  h->state = HOGP_READ_MAP;
  h->map_len = 0;
  h->map_too_long = false;
  return act(HOGP_DO_READ_LONG, h->chr[find_chr(h, HOGP_UUID_REPORT_MAP)].val);
}

void hogp_read_data(hogp_t *h, const uint8_t *data, size_t len) {
  if (!data) return;
  if (h->state == HOGP_READ_MAP) {
    size_t room = sizeof(h->map) - h->map_len;
    if (len > room) {
      h->map_too_long = true;
      len = room;
    }
    memcpy(h->map + h->map_len, data, len);
    h->map_len = (uint16_t)(h->map_len + len);
  } else if (h->state == HOGP_READ_REFS && h->next_ref < h->chrs && len >= 2) {
    h->chr[h->next_ref].report_id = data[0];
    h->chr[h->next_ref].report_type = data[1];
  }
}

// The next input report whose reference is to be read, from `from`; -1 when none is left
static int next_ref(const hogp_t *h, int from) {
  for (int i = from; i < h->chrs; i++) {
    if (h->chr[i].uuid == HOGP_UUID_REPORT && h->chr[i].ref) return i;
  }
  return -1;
}

static hogp_act_t subscribe(hogp_t *h) {
  h->state = HOGP_SUBSCRIBE;
  return write(HOGP_DO_WRITE, h->chr[h->in].cccd, 0x01, 0x00, 2);
}

// Report protocol if the map names the keyboard's report and it can notify; else boot protocol
static hogp_act_t choose(hogp_t *h) {
  h->in = -1;
  if (h->kbd.found) {
    for (int i = 0; i < h->chrs; i++) {
      const hogp_chr_t &c = h->chr[i];
      if (c.uuid != HOGP_UUID_REPORT || !c.cccd) continue;
      bool match = c.ref ? c.report_type == 1 && c.report_id == h->kbd.report_id : h->kbd.report_id == 0;
      if (match) {
        h->in = i;
        h->boot = false;
        break;
      }
    }
  }
  if (h->in < 0) {
    int b = find_chr(h, HOGP_UUID_BOOT_KBD_IN);
    if (b < 0 || !h->chr[b].cccd) return fail(h, BLE_FAIL_NOT_HID);
    h->in = b;
    h->boot = true;
  }
  int proto = find_chr(h, HOGP_UUID_PROTOCOL);
  if (proto < 0) {
    if (h->boot) return fail(h, BLE_FAIL_NOT_HID); // boot protocol cannot be chosen without it
    return subscribe(h);
  }
  h->state = HOGP_SET_PROTOCOL;
  return write(HOGP_DO_WRITE_NO_RSP, h->chr[proto].val, h->boot ? 0 : 1, 0, 1);
}

hogp_act_t hogp_read_done(hogp_t *h, int status) {
  if (h->state == HOGP_READ_MAP) {
    if (status != 0 && h->map_len == 0) {
      ble_fail_t why = gatt_fail(status);
      if (why != BLE_FAIL_NOT_HID) return fail(h, why);
    }
    if (h->map_too_long || !hid_map_parse(h->map, h->map_len, &h->kbd)) memset(&h->kbd, 0, sizeof(h->kbd));
    h->state = HOGP_READ_REFS;
    h->next_ref = next_ref(h, 0);
    if (h->next_ref < 0) return choose(h);
    return act(HOGP_DO_READ, h->chr[h->next_ref].ref);
  }
  if (h->state == HOGP_READ_REFS) {
    if (status != 0) h->chr[h->next_ref].report_type = 0; // unknown: not used
    h->next_ref = next_ref(h, h->next_ref + 1);
    if (h->next_ref < 0) return choose(h);
    return act(HOGP_DO_READ, h->chr[h->next_ref].ref);
  }
  return act(HOGP_DO_NONE);
}

hogp_act_t hogp_written(hogp_t *h, int status) {
  if (h->state == HOGP_SET_PROTOCOL) {
    (void)status; // many keyboards are in report protocol already and some refuse the write: not fatal
    return subscribe(h);
  }
  if (h->state == HOGP_SUBSCRIBE) {
    if (status != 0 && h->from_cache) return rediscover(h, h->deadline); // a timeout fails there again
    if (status != 0) return fail(h, gatt_fail(status));
    h->state = HOGP_READY;
    return act(HOGP_DO_READY);
  }
  return act(HOGP_DO_NONE);
}

hogp_act_t hogp_indicated(hogp_t *h, uint16_t attr, uint32_t now) {
  if (h->state == HOGP_IDLE || h->state == HOGP_FAILED || h->state == HOGP_SECURING || h->state == HOGP_PASSKEY) {
    return act(HOGP_DO_NONE);
  }
  if (h->svc_start && attr >= h->svc_start && attr <= h->svc_end) return act(HOGP_DO_NONE); // a HID value
  return rediscover(h, now + HOGP_DISCOVER_MS);
}

hogp_act_t hogp_tick(hogp_t *h, uint32_t now) {
  if (h->state == HOGP_IDLE || h->state == HOGP_READY || h->state == HOGP_FAILED) return act(HOGP_DO_NONE);
  if ((int32_t)(now - h->deadline) < 0) return act(HOGP_DO_NONE);
  return fail(h, h->state == HOGP_PASSKEY ? BLE_FAIL_PASSKEY_TIMEOUT : BLE_FAIL_TIMEOUT);
}

bool hogp_report(const hogp_t *h, uint16_t attr, const uint8_t *data, size_t len, uint8_t boot[8]) {
  if (h->state != HOGP_READY || h->in < 0 || attr != h->chr[h->in].val || !data) return false;
  if (h->boot) {
    if (len < 8) return false;
    memcpy(boot, data, 8);
    return true;
  }
  return hid_map_to_boot(&h->kbd, data, len, boot);
}

const char *hogp_reason_str(int status) {
  switch (status) {
  case 0: return "ok";
  case HOGP_HS_ETIMEOUT: return "no answer";
  case HOGP_HS_HCI_BASE + 0x05: return "authentication failure";
  case HOGP_HS_HCI_BASE + 0x06: return "key missing";
  case HOGP_HS_HCI_BASE + 0x08: return "supervision timeout (signal lost)";
  case HOGP_HS_HCI_BASE + 0x13: return "keyboard ended it (sleep, switch)";
  case HOGP_HS_HCI_BASE + 0x16: return "ended by the X";
  case HOGP_HS_HCI_BASE + 0x22: return "link layer timeout";
  case HOGP_HS_HCI_BASE + 0x3B: return "unacceptable connection parameters";
  case HOGP_HS_HCI_BASE + 0x3D: return "MIC failure";
  case HOGP_HS_HCI_BASE + 0x3E: return "connection failed to be established";
  }
  return "other";
}
