// A Bluetooth LE keyboard for apps on the TRMNL X (ble_keyboard.h): NimBLE as central only. The rules live in
// lib/trmnl (ble_pair.h: pairing, bond, reconnect; ble_hogp.h: security first, then HID over GATT); this file
// carries out their actions. NimBLE's callbacks run in its host task and only post messages; one task here
// owns all state, draws the pairing screen and turns reports into the app's key events.
//
// Written on NimBLE directly instead of IDF's esp_hid host (Apache-2.0, esp_hid/src/nimble_hidh.c): that one
// discovers the services right after connecting and handles no pairing, passkey or encryption events.
#ifdef BOARD_TRMNL_X

#include <Arduino.h>
#include <Preferences.h>
#include <app_api.h>
#include <ble_hogp.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <esp_wifi.h>
#include <freertos/idf_additions.h>
#include <hid_keys.h>
#include <nvs.h>
#include <sdkconfig.h>
#include <trmnl_log.h>

#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#include "app_host.h"
#include "ble_keyboard.h"
#include "keyboards.h"

// What this file needs from the configuration (sdkconfigs/sdkconfig.TRMNL_X)
#if !CONFIG_BT_NIMBLE_ENABLED || !CONFIG_BT_NIMBLE_ROLE_CENTRAL || !CONFIG_BT_NIMBLE_ROLE_OBSERVER
#error "Bluetooth keyboard: NimBLE with the central and observer roles"
#endif
#if !CONFIG_BT_NIMBLE_SECURITY_ENABLE || !CONFIG_BT_NIMBLE_SM_SC || !CONFIG_BT_NIMBLE_NVS_PERSIST
#error "Bluetooth keyboard: secure connections and bonds kept in NVS"
#endif
#if !CONFIG_ESP_COEX_SW_COEXIST_ENABLE
#error "Bluetooth keyboard: Wi-Fi and Bluetooth coexistence (typing during a Wi-Fi sync)"
#endif
#if !CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL
#error "Bluetooth keyboard: NimBLE's buffers in PSRAM"
#endif

// ble_hogp.h names NimBLE's status numbers without its headers
static_assert(HOGP_HS_ETIMEOUT == BLE_HS_ETIMEOUT, "NimBLE status");
static_assert(HOGP_HS_ATT_BASE == BLE_HS_ERR_ATT_BASE, "NimBLE status");
static_assert(HOGP_HS_HCI_BASE == BLE_HS_ERR_HCI_BASE, "NimBLE status");
static_assert(HOGP_HS_SM_US_BASE == BLE_HS_ERR_SM_US_BASE, "NimBLE status");
static_assert(HOGP_HS_SM_PEER_BASE == BLE_HS_ERR_SM_PEER_BASE, "NimBLE status");

extern "C" void ble_store_config_init(void);

// Arduino releases the controller's RAM at boot unless Bluetooth is in use; it cannot be had back later
extern "C" bool btInUse(void) { return true; }

#define TICK_MS         100
#define CONNECT_MS      8000 // one connect attempt; ble_pair retries a bonded keyboard
#define SCREEN_GAP_MS   700 // the list redraws at most this often
#define MSG_DATA        256 // an ATT value: MTU is at most 256 here
#define STOP_MS         2000
#define PREFS_NAMESPACE "ble_kbd"
#define PREFS_PEER      "peer"

typedef enum {
  M_SYNC,
  M_FOUND,
  M_CONNECTED,
  M_CONNECT_FAILED,
  M_DISCONNECTED,
  M_ENC,
  M_PASSKEY,
  M_SVC,
  M_SVC_DONE,
  M_CHR,
  M_CHR_DONE,
  M_DSC,
  M_DSC_DONE,
  M_READ_DATA,
  M_READ_DONE,
  M_WRITTEN,
  M_NOTIFY,
  M_TAP,
  M_STOP,
} msg_type_t;

typedef struct {
  msg_type_t type;
  uint16_t conn;
  int status;
  uint16_t a, b, c; // handles, uuid
  uint32_t passkey;
  ble_pair_peer_t peer;
  uint16_t len;
  uint8_t data[MSG_DATA];
} msg_t;

static QueueHandle_t msgs;
static TaskHandle_t task;
static volatile bool running, connected;
static volatile bool screen_up; // the pairing screen: taps are ours
static uint8_t own_addr_type;

// The task's state
static ble_pair_t pair;
static hogp_t hogp;
static hid_keys_t keys;
static uint16_t conn = BLE_HS_CONN_HANDLE_NONE;
static bool synced, scanning;
static uint32_t screen_drawn_at;
static bool screen_dirty;
static uint32_t pending; // ble_pair_enable's actions, carried out once NimBLE is in sync

static void post(const msg_t *m) {
  if (msgs && xQueueSend(msgs, m, 0) != pdTRUE) Log_info("BLE keys: message %d dropped", m->type);
}

static void post_simple(msg_type_t type, uint16_t c, int status, uint16_t a = 0, uint16_t b = 0, uint16_t u = 0) {
  static msg_t m; // NimBLE's host task only: one at a time
  m.type = type;
  m.conn = c;
  m.status = status;
  m.a = a;
  m.b = b;
  m.c = u;
  m.len = 0;
  post(&m);
}

static void post_data(msg_type_t type, uint16_t c, uint16_t handle, const struct os_mbuf *om) {
  static msg_t m;
  m.type = type;
  m.conn = c;
  m.status = 0;
  m.a = handle;
  uint16_t n = OS_MBUF_PKTLEN(om);
  m.len = n > MSG_DATA ? MSG_DATA : n;
  os_mbuf_copydata(om, 0, m.len, m.data);
  post(&m);
}

// ---- the bond record (the keyboard's name and address; NimBLE keeps the keys) ----

bool ble_keyboard_bond(ble_pair_peer_t *out) {
  Preferences prefs;
  if (!prefs.begin(PREFS_NAMESPACE, true)) return false;
  bool ok = prefs.getBytesLength(PREFS_PEER) == sizeof(*out) && prefs.getBytes(PREFS_PEER, out, sizeof(*out)) == sizeof(*out);
  prefs.end();
  if (ok) out->name[BLE_PAIR_NAME_MAX - 1] = '\0';
  return ok;
}

static void bond_record(const ble_pair_peer_t *peer) {
  Preferences prefs;
  if (!prefs.begin(PREFS_NAMESPACE, false)) return;
  if (peer) prefs.putBytes(PREFS_PEER, peer, sizeof(*peer));
  else prefs.remove(PREFS_PEER);
  prefs.end();
}

void ble_keyboard_forget(void) {
  bond_record(nullptr);
  if (running) {
    ble_store_clear();
  } else {
    nvs_handle_t h;
    if (nvs_open("nimble_bond", NVS_READWRITE, &h) == ESP_OK) { // NimBLE's store (ble_store_nvs.c)
      nvs_erase_all(h);
      nvs_commit(h);
      nvs_close(h);
    }
  }
  Log_info("BLE keys: keyboard forgotten");
}

static ble_addr_t to_addr(const ble_pair_peer_t *p) {
  ble_addr_t a;
  a.type = p->addr[0];
  memcpy(a.val, &p->addr[1], 6);
  return a;
}

// ---- NimBLE callbacks (host task): post only ----

static bool is_keyboard(const struct ble_hs_adv_fields *f) {
  if (f->appearance_is_present && f->appearance == 0x03C1) return true; // HID keyboard
  for (int i = 0; i < f->num_uuids16; i++) {
    if (ble_uuid_u16(&f->uuids16[i].u) == HOGP_UUID_HID_SERVICE) return true;
  }
  return false;
}

static int gap_event(struct ble_gap_event *ev, void *arg);

static int on_disc(struct ble_gap_event *ev, void *arg) {
  (void)arg;
  if (ev->type == BLE_GAP_EVENT_DISC) {
    struct ble_hs_adv_fields f;
    if (ble_hs_adv_parse_fields(&f, ev->disc.data, ev->disc.length_data) != 0) return 0;
    static msg_t m;
    m.type = M_FOUND;
    memset(&m.peer, 0, sizeof(m.peer));
    m.peer.addr[0] = ev->disc.addr.type;
    memcpy(&m.peer.addr[1], ev->disc.addr.val, 6);
    m.peer.rssi = ev->disc.rssi;
    if (f.name && f.name_len) {
      size_t n = f.name_len < BLE_PAIR_NAME_MAX - 1 ? f.name_len : BLE_PAIR_NAME_MAX - 1;
      memcpy(m.peer.name, f.name, n);
    }
    // a scan response has the name but often not the service: pass it if the name is new for a listed one
    m.status = is_keyboard(&f) ? 1 : 0;
    if (m.status || m.peer.name[0]) post(&m);
  } else if (ev->type == BLE_GAP_EVENT_DISC_COMPLETE) {
    post_simple(M_SYNC, 0, 1); // scanning ended: the task starts it again if still wanted
  }
  return 0;
}

static int on_svc(uint16_t c, const struct ble_gatt_error *e, const struct ble_gatt_svc *s, void *arg) {
  (void)arg;
  if (e->status == 0 && s) post_simple(M_SVC, c, 0, s->start_handle, s->end_handle, ble_uuid_u16(&s->uuid.u));
  else post_simple(M_SVC_DONE, c, e->status == BLE_HS_EDONE ? 0 : e->status);
  return 0;
}

static int on_chr(uint16_t c, const struct ble_gatt_error *e, const struct ble_gatt_chr *ch, void *arg) {
  (void)arg;
  if (e->status == 0 && ch) {
    uint16_t u = ch->uuid.u.type == BLE_UUID_TYPE_16 ? ble_uuid_u16(&ch->uuid.u) : 0;
    post_simple(M_CHR, c, 0, ch->val_handle, 0, u);
  } else {
    post_simple(M_CHR_DONE, c, e->status == BLE_HS_EDONE ? 0 : e->status);
  }
  return 0;
}

static int on_dsc(uint16_t c, const struct ble_gatt_error *e, uint16_t chr_val, const struct ble_gatt_dsc *d, void *arg) {
  (void)arg;
  (void)chr_val;
  if (e->status == 0 && d) {
    uint16_t u = d->uuid.u.type == BLE_UUID_TYPE_16 ? ble_uuid_u16(&d->uuid.u) : 0;
    post_simple(M_DSC, c, 0, d->handle, 0, u);
  } else {
    post_simple(M_DSC_DONE, c, e->status == BLE_HS_EDONE ? 0 : e->status);
  }
  return 0;
}

static int on_read(uint16_t c, const struct ble_gatt_error *e, struct ble_gatt_attr *attr, void *arg) {
  bool is_long = arg != nullptr;
  if (e->status == 0 && attr && attr->om) {
    post_data(M_READ_DATA, c, attr->handle, attr->om);
    if (!is_long) post_simple(M_READ_DONE, c, 0);
  } else {
    post_simple(M_READ_DONE, c, e->status == BLE_HS_EDONE ? 0 : e->status);
  }
  return 0;
}

static int on_write(uint16_t c, const struct ble_gatt_error *e, struct ble_gatt_attr *attr, void *arg) {
  (void)attr;
  (void)arg;
  post_simple(M_WRITTEN, c, e->status);
  return 0;
}

static int gap_event(struct ble_gap_event *ev, void *arg) {
  (void)arg;
  switch (ev->type) {
  case BLE_GAP_EVENT_CONNECT:
    if (ev->connect.status == 0) post_simple(M_CONNECTED, ev->connect.conn_handle, 0);
    else post_simple(M_CONNECT_FAILED, 0, ev->connect.status);
    break;
  case BLE_GAP_EVENT_DISCONNECT:
    post_simple(M_DISCONNECTED, ev->disconnect.conn.conn_handle, ev->disconnect.reason);
    break;
  case BLE_GAP_EVENT_ENC_CHANGE:
    post_simple(M_ENC, ev->enc_change.conn_handle, ev->enc_change.status);
    break;
  case BLE_GAP_EVENT_PASSKEY_ACTION:
    if (ev->passkey.params.action == BLE_SM_IOACT_DISP) {
      struct ble_sm_io io = {};
      io.action = BLE_SM_IOACT_DISP;
      io.passkey = esp_random() % 1000000;
      int rc = ble_sm_inject_io(ev->passkey.conn_handle, &io);
      static msg_t m;
      m.type = M_PASSKEY;
      m.conn = ev->passkey.conn_handle;
      m.passkey = io.passkey;
      m.status = rc;
      post(&m);
    }
    break;
  case BLE_GAP_EVENT_REPEAT_PAIRING:
    // The keyboard wants to pair again although we hold a bond: it lost its side. The rules delete ours and
    // list the keyboards again (ble_pair.h), so this pairing is not let through
    post_simple(M_ENC, ev->repeat_pairing.conn_handle, BLE_HS_ERR_HCI_BASE + BLE_ERR_PINKEY_MISSING);
    return BLE_GAP_REPEAT_PAIRING_IGNORE;
  case BLE_GAP_EVENT_NOTIFY_RX:
    if (ev->notify_rx.om) post_data(M_NOTIFY, ev->notify_rx.conn_handle, ev->notify_rx.attr_handle, ev->notify_rx.om);
    break;
  default:
    break;
  }
  return 0;
}

static void on_sync(void) {
  ble_hs_util_ensure_addr(0);
  ble_hs_id_infer_auto(0, &own_addr_type);
  post_simple(M_SYNC, 0, 0);
}

static void on_reset(int reason) { Log_error("BLE keys: host reset (%d)", reason); }

static void host_task(void *arg) {
  (void)arg;
  nimble_port_run(); // until nimble_port_stop
  nimble_port_freertos_deinit();
}

// ---- carrying out the rules' actions (the task) ----

static void scan(bool on) {
  if (!synced || on == scanning) return;
  if (on) {
    struct ble_gap_disc_params dp = {};
    dp.passive = 0; // scan responses carry the name
    dp.filter_duplicates = 0; // the signal keeps updating
    dp.itvl = 0x50; // a 30 ms window every 50 ms: Wi-Fi gets the rest
    dp.window = 0x30;
    int rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &dp, on_disc, nullptr);
    scanning = rc == 0;
    if (rc) Log_error("BLE keys: scan failed (%d)", rc);
  } else {
    ble_gap_disc_cancel();
    scanning = false;
  }
}

static void drop_link(void) {
  if (conn != BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
  else if (ble_gap_conn_active()) ble_gap_conn_cancel();
}

static void save_bond(void) {
  struct ble_gap_conn_desc d;
  if (conn != BLE_HS_CONN_HANDLE_NONE && ble_gap_conn_find(conn, &d) == 0) { // the identity NimBLE stored the keys under
    pair.target.addr[0] = d.peer_id_addr.type;
    memcpy(&pair.target.addr[1], d.peer_id_addr.val, 6);
  }
  ble_addr_t keep = to_addr(&pair.target);
  ble_addr_t peers[MYNEWT_VAL(BLE_STORE_MAX_BONDS)];
  int n = 0;
  if (ble_store_util_bonded_peers(peers, &n, MYNEWT_VAL(BLE_STORE_MAX_BONDS)) == 0) {
    for (int i = 0; i < n; i++) {
      if (ble_addr_cmp(&peers[i], &keep) != 0) ble_store_util_delete_peer(&peers[i]); // one keyboard only
    }
  }
  bond_record(&pair.target);
  Log_info("BLE keys: paired with %s", pair.target.name);
}

static void apply(uint32_t act) {
  if (act & BLE_ACT_DELETE_BOND) {
    ble_addr_t a = to_addr(&pair.target);
    ble_store_util_delete_peer(&a);
    bond_record(nullptr);
    Log_info("BLE keys: bond with %s deleted", pair.target.name);
  }
  if (act & BLE_ACT_SAVE_BOND) save_bond();
  if (act & BLE_ACT_RELEASE) {
    app_event_t ev[HID_KEYS_MAX_EVENTS];
    keyboards_post(ev, hid_keys_release_all(&keys, ev));
    if (connected) {
      connected = false;
      Log_info("BLE keys: keyboard gone");
      keyboards_post_change();
    }
  }
  if (act & BLE_ACT_DISCONNECT) drop_link();
  scan(pair.state == BLE_PAIR_LIST); // BLE_ACT_SCAN is set exactly while listing
  if (act & BLE_ACT_CONNECT) {
    scan(false);
    ble_addr_t a = to_addr(&pair.target);
    int rc = ble_gap_connect(own_addr_type, &a, CONNECT_MS, nullptr, gap_event, nullptr);
    if (rc) {
      Log_error("BLE keys: connect failed to start (%d)", rc);
      apply(ble_pair_failed(&pair, BLE_FAIL_TIMEOUT, millis()));
    }
  }
  if (act & BLE_ACT_SCREEN) {
    bool want = pair.state == BLE_PAIR_LIST || pair.state == BLE_PAIR_CONNECTING || pair.state == BLE_PAIR_PASSKEY;
    if (!want) {
      screen_up = false;
      ble_screen_release();
    } else if (!pair.bonded || pair.state != BLE_PAIR_CONNECTING) {
      screen_up = true;
      screen_dirty = true; // drawn by the loop, at most every SCREEN_GAP_MS
    }
  }
}

static void hogp_do(const hogp_act_t &a) {
  int rc = 0;
  switch (a.what) {
  case HOGP_DO_NONE: return;
  case HOGP_DO_SECURE: rc = ble_gap_security_initiate(conn); break;
  case HOGP_DO_FIND_SERVICE: {
    static const ble_uuid16_t hid = BLE_UUID16_INIT(HOGP_UUID_HID_SERVICE);
    rc = ble_gattc_disc_svc_by_uuid(conn, &hid.u, on_svc, nullptr);
    break;
  }
  case HOGP_DO_FIND_CHRS: rc = ble_gattc_disc_all_chrs(conn, a.handle, a.end, on_chr, nullptr); break;
  case HOGP_DO_FIND_DSCS: rc = ble_gattc_disc_all_dscs(conn, a.handle, a.end, on_dsc, nullptr); break;
  case HOGP_DO_READ: rc = ble_gattc_read(conn, a.handle, on_read, nullptr); break;
  case HOGP_DO_READ_LONG: rc = ble_gattc_read_long(conn, a.handle, 0, on_read, (void *)1); break;
  case HOGP_DO_WRITE_NO_RSP:
    rc = ble_gattc_write_no_rsp_flat(conn, a.handle, a.value, a.len);
    hogp_do(hogp_written(&hogp, rc));
    return;
  case HOGP_DO_WRITE: rc = ble_gattc_write_flat(conn, a.handle, a.value, a.len, on_write, nullptr); break;
  case HOGP_DO_READY:
    hid_keys_init(&keys);
    connected = true;
    Log_info("BLE keys: %s ready (%s protocol)", pair.target.name, hogp.boot ? "boot" : "report");
    apply(ble_pair_connected(&pair));
    screen_up = false;
    ble_screen_release(); // also after a reconnect that showed nothing: a no-op then
    keyboards_post_change();
    return;
  case HOGP_DO_FAIL:
    Log_info("BLE keys: %s failed (%d)", pair.target.name, a.why);
    drop_link();
    apply(ble_pair_failed(&pair, a.why, millis()));
    return;
  }
  if (rc != 0) { // the request did not start: as if it failed
    Log_error("BLE keys: request %d failed to start (%d)", a.what, rc);
    hogp_act_t f = {};
    f.what = HOGP_DO_FAIL;
    f.why = a.what == HOGP_DO_SECURE ? hogp_enc_fail(rc, false) : BLE_FAIL_TIMEOUT;
    hogp.state = HOGP_FAILED;
    hogp_do(f);
  }
}

static void handle(const msg_t &m) {
  uint32_t now = millis();
  bool ours = m.conn == conn && conn != BLE_HS_CONN_HANDLE_NONE;
  switch (m.type) {
  case M_SYNC:
    if (m.status == 0) synced = true;
    else scanning = false; // the scan ended by itself
    apply(pending); // the first actions, or only scanning again if listing
    pending = 0;
    break;
  case M_FOUND:
  {
    if (pair.state != BLE_PAIR_LIST) break;
    // a scan response with only a name updates a listed keyboard; it is not added
    bool listed = false;
    for (int i = 0; i < pair.found_count && !m.status; i++) {
      listed = listed || memcmp(pair.found[i].addr, m.peer.addr, sizeof(m.peer.addr)) == 0;
    }
    if (m.status || listed) apply(ble_pair_found(&pair, &m.peer));
    break;
  }
  case M_CONNECTED:
    conn = m.conn;
    hogp_do(hogp_start(&hogp, now));
    break;
  case M_CONNECT_FAILED:
    apply(ble_pair_failed(&pair, BLE_FAIL_TIMEOUT, now));
    break;
  case M_DISCONNECTED:
    if (m.conn != conn) break;
    conn = BLE_HS_CONN_HANDLE_NONE;
    if (hogp.state == HOGP_SECURING || hogp.state == HOGP_PASSKEY) { // dropped while securing: why
      apply(ble_pair_failed(&pair, hogp_enc_fail(m.status, hogp.state == HOGP_PASSKEY), now));
    } else {
      apply(ble_pair_disconnected(&pair, now));
    }
    hogp.state = HOGP_IDLE;
    break;
  case M_ENC:
    if (ours) hogp_do(hogp_encrypted(&hogp, m.status, now));
    break;
  case M_PASSKEY:
    if (!ours) break;
    hogp_passkey(&hogp, now);
    if (pair.bonded) { // a bonded keyboard asking for a passkey has lost its side of the bond
      hogp_do(hogp_encrypted(&hogp, BLE_HS_ERR_HCI_BASE + BLE_ERR_PINKEY_MISSING, now));
      break;
    }
    Log_info("BLE keys: passkey shown for %s", pair.target.name);
    apply(ble_pair_passkey(&pair, m.passkey));
    break;
  case M_SVC:
    if (ours) hogp_service(&hogp, m.c, m.a, m.b);
    break;
  case M_SVC_DONE:
    if (ours) hogp_do(hogp_services_done(&hogp, m.status));
    break;
  case M_CHR:
    if (ours) hogp_chr(&hogp, m.c, m.a);
    break;
  case M_CHR_DONE:
    if (ours) hogp_do(hogp_chrs_done(&hogp, m.status));
    break;
  case M_DSC:
    if (ours) hogp_dsc(&hogp, m.a, m.c);
    break;
  case M_DSC_DONE:
    if (ours) hogp_do(hogp_dscs_done(&hogp, m.status));
    break;
  case M_READ_DATA:
    if (ours) hogp_read_data(&hogp, m.data, m.len);
    break;
  case M_READ_DONE:
    if (ours) hogp_do(hogp_read_done(&hogp, m.status));
    break;
  case M_WRITTEN:
    if (ours) hogp_do(hogp_written(&hogp, m.status));
    break;
  case M_NOTIFY: {
    uint8_t boot[HID_KEYS_REPORT];
    if (!ours || !hogp_report(&hogp, m.a, m.data, m.len, boot)) break;
    app_event_t ev[HID_KEYS_MAX_EVENTS];
    keyboards_post(ev, hid_keys_report(&keys, boot, sizeof(boot), ev));
    break;
  }
  case M_TAP:
    apply(ble_pair_touch(&pair, m.status));
    break;
  case M_STOP:
    break;
  }
}

static void kbd_loop(void *arg) {
  (void)arg;
  msg_t *m = (msg_t *)heap_caps_malloc(sizeof(msg_t), MALLOC_CAP_SPIRAM);
  bool stop = false;
  while (m && !stop) {
    if (xQueueReceive(msgs, m, pdMS_TO_TICKS(TICK_MS)) == pdTRUE) {
      stop = m->type == M_STOP;
      if (!stop) handle(*m);
      continue;
    }
    uint32_t now = millis();
    hogp_do(hogp_tick(&hogp, now));
    apply(ble_pair_tick(&pair, now));
    if (screen_up && screen_dirty && now - screen_drawn_at >= SCREEN_GAP_MS) {
      screen_dirty = false;
      screen_drawn_at = now;
      ble_screen_show(&pair);
    }
  }
  heap_caps_free(m);
  // the app ended: drop the link and the radio
  apply(ble_pair_disable(&pair));
  scan(false);
  uint32_t start = millis();
  while (conn != BLE_HS_CONN_HANDLE_NONE && millis() - start < STOP_MS) { // wait for the disconnect
    msg_t d;
    if (xQueueReceive(msgs, &d, pdMS_TO_TICKS(50)) == pdTRUE && d.type == M_DISCONNECTED && d.conn == conn) {
      conn = BLE_HS_CONN_HANDLE_NONE;
    }
  }
  screen_up = false;
  ble_screen_release();
  task = nullptr;
  vTaskDelete(nullptr);
}

// ---- start and stop ----

static bool radio_start(void) {
  size_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  esp_err_t rc = nimble_port_init(); // controller and host
  if (rc != ESP_OK) {
    Log_error("BLE keys: NimBLE init failed: %s", esp_err_to_name(rc));
    return false;
  }
  ble_hs_cfg.sync_cb = on_sync;
  ble_hs_cfg.reset_cb = on_reset;
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
  ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY; // the X shows a passkey, the keyboard types it
  ble_hs_cfg.sm_bonding = 1;
  ble_hs_cfg.sm_mitm = 1; // passkey when the keyboard can type; just-works otherwise
  ble_hs_cfg.sm_sc = 1;
  ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  ble_store_config_init();
  nimble_port_freertos_init(host_task);
  esp_wifi_set_ps(WIFI_PS_MIN_MODEM); // Wi-Fi already up shares the radio (an error when it is off: ignored)
  size_t after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  Log_info("BLE keys: radio on; internal RAM %u bytes used, %u free (largest %u), PSRAM free %u",
           (unsigned)(before - after), (unsigned)after, (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  return true;
}

static void radio_stop(void) {
  synced = scanning = false;
  if (nimble_port_stop() == 0) nimble_port_deinit();
  Log_info("BLE keys: radio off; internal RAM %u free", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

int ble_keyboard_enable(bool on) {
  if (!on) {
    ble_keyboard_end();
    return 0;
  }
  if (running) return 0;
  if (!msgs) msgs = xQueueCreateWithCaps(24, sizeof(msg_t), MALLOC_CAP_SPIRAM);
  if (!msgs) return APP_ENOMEM;
  xQueueReset(msgs);
  ble_pair_peer_t bond;
  bool bonded = ble_keyboard_bond(&bond);
  memset(&hogp, 0, sizeof(hogp));
  conn = BLE_HS_CONN_HANDLE_NONE;
  connected = false;
  screen_up = false;
  screen_drawn_at = 0;
  hid_keys_init(&keys);
  // The rules first; their first actions wait in the task until NimBLE is in sync
  pending = ble_pair_enable(&pair, bonded ? &bond : nullptr);
  synced = scanning = false;
  if (!radio_start()) return APP_ENOMEM;
  running = true;
  if (xTaskCreatePinnedToCore(kbd_loop, "ble_kbd", 8192, nullptr, 3, &task, 0) != pdPASS) {
    running = false;
    radio_stop();
    return APP_ENOMEM;
  }
  return 0;
}

bool ble_keyboard_connected(void) { return connected; }
bool ble_keyboard_radio_on(void) { return running; }

bool ble_keyboard_tap(int zone) {
  if (!running || !screen_up) return false;
  msg_t m = {};
  m.type = M_TAP;
  m.status = zone;
  post(&m);
  return true;
}

void ble_keyboard_end(void) {
  if (!running) return;
  msg_t m = {};
  m.type = M_STOP;
  xQueueSendToFront(msgs, &m, pdMS_TO_TICKS(100));
  uint32_t start = millis();
  while (task && millis() - start < 2 * STOP_MS) delay(10);
  if (task) {
    Log_error("BLE keys: task did not end - restarting");
    esp_restart();
  }
  connected = false;
  radio_stop();
  running = false;
}

#endif // BOARD_TRMNL_X
