#include <string.h>
#include <trmnl_crc32.h>
#include <unity.h>

#include <string>

#include "memfs.h"
#include <webshare_auth.h>
#include <stdio.h>
#include <webshare_mode.h>
#include <webshare_names.h>
#include <webshare_store.h>
#include <webshare_zip.h>

static std::string up(MemFs &m, const char *path, const std::string &data, ws_put_t *how = nullptr,
                      char *final_out = nullptr, ws_result_t *rc = nullptr) {
  ws_upload_t u;
  memset(&u, 0, sizeof(u));
  ws_put_t h = WS_PUT_NEW;
  char fin[WS_PATH_MAX + 1] = "";
  ws_result_t r = ws_upload_begin(&u, &m.fs, path, (uint32_t)data.size());
  for (size_t i = 0; r == WS_OK && i < data.size(); i += 100) {
    r = ws_upload_chunk(&u, data.data() + i, data.size() - i < 100 ? data.size() - i : 100);
  }
  if (r == WS_OK) r = ws_upload_end(&u, &h, fin);
  if (how) *how = h;
  if (final_out) strcpy(final_out, fin);
  if (rc) *rc = r;
  return fin;
}

// ---- names ----

void test_classify_layout() {
  TEST_ASSERT_EQUAL(WS_DOC, ws_classify("documents/Novel/Chapter 1.md"));
  TEST_ASSERT_EQUAL(WS_DOC, ws_classify("documents/Notes/a.TXT"));
  TEST_ASSERT_EQUAL(WS_FONT, ws_classify("settings/fonts/Serif.ttf"));
  TEST_ASSERT_EQUAL(WS_OTHER, ws_classify("settings/settings.json"));
  TEST_ASSERT_EQUAL(WS_OTHER, ws_classify("documents/Novel/cover.png"));
  TEST_ASSERT_EQUAL(WS_OTHER, ws_classify("documents/loose.md"));
  TEST_ASSERT_EQUAL(WS_OTHER, ws_classify("documents/a/b/c.md"));
  TEST_ASSERT_EQUAL(WS_OTHER, ws_classify("documents/Novel/.md"));
}

void test_bad_names_are_refused() {
  const char *bad[] = {"../documents/a/b.md", "documents/../x/b.md", "documents//b.md", "/documents/a/b.md",
                       "documents/a/.hidden.md", "documents/a/b.md.", "documents/a/b .md ", "documents/a\\b/c.md",
                       "documents/a/b:c.md", "documents/a/b*.md", "documents/a/b?.md", "documents/a/b\"c.md",
                       "documents/a/b|c.md", "documents/a/b\x01.md", "documents/a/b\x7f.md", "documents/./b.md",
                       "documents/a/.webshare.tmp", "", "documents/a/"};
  for (const char *p : bad) TEST_ASSERT_EQUAL_MESSAGE(WS_OTHER, ws_classify(p), p);
  TEST_ASSERT_EQUAL(WS_OTHER, ws_classify(nullptr));
  std::string longname = std::string("documents/a/") + std::string(60, 'x') + ".md";
  TEST_ASSERT_EQUAL(WS_OTHER, ws_classify(longname.c_str()));
  std::string ok40 = std::string("documents/a/") + std::string(37, 'x') + ".md";
  TEST_ASSERT_EQUAL(WS_DOC, ws_classify(ok40.c_str()));
  TEST_ASSERT_EQUAL(WS_DOC, ws_classify("documents/Caf\xc3\xa9/\xc3\x89t\xc3\xa9.md")); // UTF-8 is fine
}

void test_alt_name() {
  char out[80];
  TEST_ASSERT_TRUE(ws_alt_name("documents/Novel/Chapter 1.md", 2, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("documents/Novel/Chapter 1 (web 2).md", out);
  std::string full = std::string("documents/a/") + std::string(37, 'x') + ".md"; // 40 bytes: no room
  TEST_ASSERT_FALSE(ws_alt_name(full.c_str(), 1, out, sizeof(out)));
  TEST_ASSERT_FALSE(ws_alt_name("settings/settings.json", 1, out, sizeof(out)));
  TEST_ASSERT_FALSE(ws_alt_name("documents/a/b.md", 100, out, sizeof(out)));
}

// ---- auth ----

static const uint32_t TR[2] = {0xdeadbeef, 0x01234567};

void test_pin_is_six_digits_and_checked() {
  ws_auth_t a;
  ws_auth_init(&a, 42);
  TEST_ASSERT_EQUAL_STRING("000042", a.pin);
  char tok[WS_TOKEN_LEN + 1];
  TEST_ASSERT_EQUAL(WS_AUTH_WRONG, ws_auth_login(&a, "000041", 0, TR, tok));
  TEST_ASSERT_EQUAL(WS_AUTH_WRONG, ws_auth_login(&a, "42", 0, TR, tok));
  TEST_ASSERT_EQUAL(WS_AUTH_WRONG, ws_auth_login(&a, "0000420", 0, TR, tok));
  ws_auth_init(&a, 42); // (five wrong codes in a row would lock)
  TEST_ASSERT_EQUAL(WS_AUTH_WRONG, ws_auth_login(&a, "", 0, TR, tok));
  TEST_ASSERT_EQUAL(WS_AUTH_WRONG, ws_auth_login(&a, nullptr, 0, TR, tok));
  TEST_ASSERT_FALSE(ws_auth_session_ok(&a, "deadbeef01234567"));
  TEST_ASSERT_EQUAL(WS_AUTH_OK, ws_auth_login(&a, "000042", 0, TR, tok));
  TEST_ASSERT_EQUAL_STRING("deadbeef01234567", tok);
  TEST_ASSERT_TRUE(ws_auth_session_ok(&a, tok));
  TEST_ASSERT_FALSE(ws_auth_session_ok(&a, "deadbeef01234568"));
  TEST_ASSERT_FALSE(ws_auth_session_ok(&a, ""));
  TEST_ASSERT_FALSE(ws_auth_session_ok(&a, nullptr));
}

void test_wrong_codes_lock_then_die() {
  ws_auth_t a;
  ws_auth_init(&a, 123456);
  char tok[WS_TOKEN_LEN + 1];
  uint32_t now = 1000;
  for (int i = 0; i < WS_FAILS_PER_ROUND - 1; i++) {
    TEST_ASSERT_EQUAL(WS_AUTH_WRONG, ws_auth_login(&a, "999999", now, TR, tok));
  }
  TEST_ASSERT_EQUAL(WS_AUTH_LOCKED, ws_auth_login(&a, "999999", now, TR, tok));
  // locked: even the right code is not looked at
  TEST_ASSERT_EQUAL(WS_AUTH_LOCKED, ws_auth_login(&a, "123456", now + 1000, TR, tok));
  TEST_ASSERT_EQUAL_UINT32(WS_LOCK_MS - 1000, ws_auth_lock_left(&a, now + 1000));
  // after the lock the right code works, and the clock may wrap
  TEST_ASSERT_EQUAL(WS_AUTH_OK, ws_auth_login(&a, "123456", now + WS_LOCK_MS, TR, tok));
  // keep failing: the second lock is twice as long, then the page is dead
  ws_auth_t b;
  ws_auth_init(&b, 123456);
  now = 0xFFFFF000u; // near the wrap
  int locks = 0;
  for (int i = 0; i < 100; i++) {
    ws_auth_result_t r = ws_auth_login(&b, "999999", now, TR, tok);
    if (r == WS_AUTH_LOCKED) {
      locks++;
      if (locks == 2) TEST_ASSERT_EQUAL_UINT32(2 * WS_LOCK_MS, ws_auth_lock_left(&b, now));
      now += 3 * WS_LOCK_MS;
    }
    if (r == WS_AUTH_DEAD) break;
  }
  TEST_ASSERT_EQUAL(WS_AUTH_DEAD, ws_auth_login(&b, "123456", now + 99999999u, TR, tok));
  TEST_ASSERT_TRUE(b.fails <= WS_MAX_FAILS);
}

void test_sessions_are_capped() {
  ws_auth_t a;
  ws_auth_init(&a, 7);
  char t[WS_SESSIONS + 1][WS_TOKEN_LEN + 1];
  for (int i = 0; i <= WS_SESSIONS; i++) {
    uint32_t r[2] = {(uint32_t)i + 1, 77};
    TEST_ASSERT_EQUAL(WS_AUTH_OK, ws_auth_login(&a, "000007", 0, r, t[i]));
  }
  TEST_ASSERT_FALSE(ws_auth_session_ok(&a, t[0])); // the oldest went
  for (int i = 1; i <= WS_SESSIONS; i++) TEST_ASSERT_TRUE(ws_auth_session_ok(&a, t[i]));
}

void test_idle_timer() {
  TEST_ASSERT_FALSE(ws_idle_expired(1000, 1000 + WS_IDLE_MS - 1));
  TEST_ASSERT_TRUE(ws_idle_expired(1000, 1000 + WS_IDLE_MS));
  TEST_ASSERT_FALSE(ws_idle_expired(0xFFFFFF00u, 0x100)); // across the millis wrap
  TEST_ASSERT_TRUE(ws_idle_expired(0xFFFFFF00u, 0xFFFFFF00u + WS_IDLE_MS));
}

// ---- mode and one-time token ----

void test_home_when_wifi_is_up_ap_when_not() {
  app_webshare_mode_t m;
  TEST_ASSERT_TRUE(ws_pick_mode(APP_WEBSHARE_AUTO, true, &m));
  TEST_ASSERT_EQUAL(APP_WEBSHARE_HOME, m);
  TEST_ASSERT_TRUE(ws_pick_mode(APP_WEBSHARE_AUTO, false, &m));
  TEST_ASSERT_EQUAL(APP_WEBSHARE_AP, m);
  TEST_ASSERT_FALSE(ws_pick_mode(APP_WEBSHARE_HOME, false, &m)); // asked for home, no fallback
  TEST_ASSERT_TRUE(ws_pick_mode(APP_WEBSHARE_HOME, true, &m));
  TEST_ASSERT_TRUE(ws_pick_mode(APP_WEBSHARE_AP, true, &m));
  TEST_ASSERT_EQUAL(APP_WEBSHARE_AP, m);
  TEST_ASSERT_FALSE(ws_pick_mode((app_webshare_mode_t)9, true, &m));
}

static const uint32_t T4[4] = {1, 2, 3, 0xabcdef01};

void test_login_token_works_once_then_needs_the_cookie() {
  ws_auth_t a;
  ws_auth_init(&a, 5);
  ws_auth_new_token(&a, T4, 1000);
  TEST_ASSERT_EQUAL_STRING("000000010000000200000003abcdef01", a.login_token);
  char sid[WS_TOKEN_LEN + 1];
  TEST_ASSERT_EQUAL(WS_AUTH_OK, ws_auth_token_login(&a, a.login_token, 2000, TR, sid));
  TEST_ASSERT_TRUE(ws_auth_session_ok(&a, sid));
  TEST_ASSERT_EQUAL(WS_AUTH_WRONG, ws_auth_token_login(&a, "000000010000000200000003abcdef01", 3000, TR, sid)); // used
  TEST_ASSERT_EQUAL(WS_AUTH_WRONG, ws_auth_token_login(&a, "", 3000, TR, sid)); // empty is not a match for a used one
  TEST_ASSERT_EQUAL(WS_AUTH_OK, ws_auth_login(&a, "000005", 3000, TR, sid)); // the code still works
}

void test_wrong_or_expired_token_is_refused_and_counts() {
  ws_auth_t a;
  ws_auth_init(&a, 5);
  ws_auth_new_token(&a, T4, 1000);
  char sid[WS_TOKEN_LEN + 1] = "";
  TEST_ASSERT_EQUAL(WS_AUTH_WRONG, ws_auth_token_login(&a, "000000010000000200000003abcdef02", 1100, TR, sid));
  TEST_ASSERT_EQUAL(WS_AUTH_WRONG, ws_auth_token_login(&a, "short", 1100, TR, sid));
  TEST_ASSERT_EQUAL(WS_AUTH_WRONG, ws_auth_token_login(&a, nullptr, 1100, TR, sid));
  TEST_ASSERT_EQUAL(2 + 1, a.fails);
  TEST_ASSERT_EQUAL_STRING("", sid);
  // expired: right token, but 15 minutes later
  TEST_ASSERT_EQUAL(WS_AUTH_WRONG, ws_auth_token_login(&a, "000000010000000200000003abcdef01", 1000 + WS_IDLE_MS, TR, sid));
  // just before: fine
  ws_auth_t b;
  ws_auth_init(&b, 5);
  ws_auth_new_token(&b, T4, 1000);
  TEST_ASSERT_EQUAL(WS_AUTH_OK, ws_auth_token_login(&b, "000000010000000200000003abcdef01", 1000 + WS_IDLE_MS - 1, TR, sid));
  // no token made at all (stopped and not restarted): nothing matches
  ws_auth_t c;
  ws_auth_init(&c, 5);
  TEST_ASSERT_EQUAL(WS_AUTH_WRONG, ws_auth_token_login(&c, "", 0, TR, sid));
}

void test_token_guesses_lock_like_wrong_codes() {
  ws_auth_t a;
  ws_auth_init(&a, 5);
  ws_auth_new_token(&a, T4, 0);
  char sid[WS_TOKEN_LEN + 1];
  for (int i = 0; i < WS_FAILS_PER_ROUND - 1; i++) ws_auth_token_login(&a, "x", 10, TR, sid);
  TEST_ASSERT_EQUAL(WS_AUTH_LOCKED, ws_auth_token_login(&a, "x", 10, TR, sid));
  TEST_ASSERT_EQUAL(WS_AUTH_LOCKED, ws_auth_token_login(&a, "000000010000000200000003abcdef01", 20, TR, sid));
}

// The firmware must never write the code, the token or the login link to the log: no Log_ line in
// src/webshare.cpp may mention them (the file is read from the project root, where pio runs the tests)
void test_firmware_never_logs_token_or_code() {
  FILE *f = fopen("src/webshare.cpp", "r");
  TEST_ASSERT_NOT_NULL_MESSAGE(f, "src/webshare.cpp not found");
  char line[512];
  int logs = 0;
  while (fgets(line, sizeof(line), f)) {
    if (!strstr(line, "Log_") && !strstr(line, "Serial.")) continue;
    logs++;
    for (const char *w : {"token", "login", "pin", "code", "auth", "cookie", "wsid", "S.url", "status"}) {
      TEST_ASSERT_NULL_MESSAGE(strstr(line, w), line);
    }
  }
  fclose(f);
  TEST_ASSERT_TRUE(logs >= 5); // the check really saw the log lines
}

// ---- upload ----

void test_upload_new_file_is_atomic_and_clean() {
  MemFs m;
  ws_put_t how;
  char fin[80];
  up(m, "documents/Novel/Ch 1.md", "hello world", &how, fin);
  TEST_ASSERT_EQUAL(WS_PUT_NEW, how);
  TEST_ASSERT_EQUAL_STRING("hello world", m.get("documents/Novel/Ch 1.md").c_str());
  TEST_ASSERT_FALSE(m.has("documents/Novel/" WS_TMP_NAME));
  TEST_ASSERT_EQUAL(0, (int)m.open_handles.size());
}

void test_upload_never_overwrites_different_file() {
  MemFs m;
  m.put("documents/Novel/Ch 1.md", "mine");
  ws_put_t how;
  char fin[80];
  up(m, "documents/Novel/Ch 1.md", "from the phone", &how, fin);
  TEST_ASSERT_EQUAL(WS_PUT_KEPT_BOTH, how);
  TEST_ASSERT_EQUAL_STRING("documents/Novel/Ch 1 (web 1).md", fin);
  TEST_ASSERT_EQUAL_STRING("mine", m.get("documents/Novel/Ch 1.md").c_str());
  TEST_ASSERT_EQUAL_STRING("from the phone", m.get(fin).c_str());
  up(m, "documents/Novel/Ch 1.md", "again, other", &how, fin); // the next free number
  TEST_ASSERT_EQUAL_STRING("documents/Novel/Ch 1 (web 2).md", fin);
  TEST_ASSERT_EQUAL_STRING("from the phone", m.get("documents/Novel/Ch 1 (web 1).md").c_str());
}

void test_upload_of_same_bytes_writes_nothing() {
  MemFs m;
  m.put("documents/Novel/Ch 1.md", "same");
  ws_put_t how;
  char fin[80];
  up(m, "documents/Novel/Ch 1.md", "same", &how, fin);
  TEST_ASSERT_EQUAL(WS_PUT_SAME, how);
  TEST_ASSERT_EQUAL(1, (int)m.files().size());
  up(m, "documents/Novel/Ch 1.md", "samf", &how, fin); // same size, other bytes: kept both
  TEST_ASSERT_EQUAL(WS_PUT_KEPT_BOTH, how);
}

void test_upload_refuses_names_and_sizes() {
  MemFs m;
  ws_result_t rc;
  up(m, "settings/settings.json", "{}", nullptr, nullptr, &rc);
  TEST_ASSERT_EQUAL(WS_E_NAME, rc);
  up(m, "../evil.md", "x", nullptr, nullptr, &rc);
  TEST_ASSERT_EQUAL(WS_E_NAME, rc);
  up(m, "documents/a/b.exe", "x", nullptr, nullptr, &rc);
  TEST_ASSERT_EQUAL(WS_E_NAME, rc);
  ws_upload_t u;
  memset(&u, 0, sizeof(u));
  TEST_ASSERT_EQUAL(WS_E_TOO_BIG, ws_upload_begin(&u, &m.fs, "documents/a/b.md", WS_DOC_MAX + 1));
  TEST_ASSERT_EQUAL(WS_E_TOO_BIG, ws_upload_begin(&u, &m.fs, "settings/fonts/a.ttf", WS_FONT_MAX + 1));
  TEST_ASSERT_EQUAL(0, (int)m.files().size());
}

void test_upload_refused_when_it_would_fill_the_disk() {
  MemFs m;
  m.capacity = 10 * 1024; // less than the margin
  ws_result_t rc;
  up(m, "documents/a/b.md", "x", nullptr, nullptr, &rc);
  TEST_ASSERT_EQUAL(WS_E_NO_SPACE, rc);
  TEST_ASSERT_EQUAL(0, (int)m.files().size());
}

void test_full_disk_midway_leaves_old_file_and_no_temp() {
  MemFs m;
  m.put("documents/a/b.md", "old");
  m.fail_write_after = 150; // announced fits, the disk fills anyway
  ws_result_t rc;
  up(m, "documents/a/b.md", std::string(400, 'n'), nullptr, nullptr, &rc);
  TEST_ASSERT_EQUAL(WS_E_NO_SPACE, rc);
  TEST_ASSERT_EQUAL_STRING("old", m.get("documents/a/b.md").c_str());
  TEST_ASSERT_FALSE(m.has("documents/a/" WS_TMP_NAME));
  TEST_ASSERT_EQUAL(0, (int)m.open_handles.size());
}

void test_close_failure_means_no_file() {
  MemFs m;
  m.fail_close = true;
  ws_result_t rc;
  up(m, "documents/a/b.md", "data", nullptr, nullptr, &rc);
  TEST_ASSERT_EQUAL(WS_E_NO_SPACE, rc);
  TEST_ASSERT_FALSE(m.has("documents/a/b.md"));
  TEST_ASSERT_FALSE(m.has("documents/a/" WS_TMP_NAME));
}

void test_silent_loss_at_close_is_caught() {
  MemFs m;
  m.put("documents/a/b.md", "old");
  m.close_loses_last_byte = true; // no error from close, but the file is short
  ws_result_t rc;
  up(m, "documents/a/b.md", "new data", nullptr, nullptr, &rc);
  TEST_ASSERT_EQUAL(WS_E_NO_SPACE, rc);
  TEST_ASSERT_EQUAL_STRING("old", m.get("documents/a/b.md").c_str());
  TEST_ASSERT_FALSE(m.has("documents/a/" WS_TMP_NAME));
}

void test_cable_pulled_midway_drops_the_temp() {
  MemFs m;
  m.put("documents/a/b.md", "old");
  ws_upload_t u;
  memset(&u, 0, sizeof(u));
  TEST_ASSERT_EQUAL(WS_OK, ws_upload_begin(&u, &m.fs, "documents/a/b.md", 100));
  TEST_ASSERT_EQUAL(WS_OK, ws_upload_chunk(&u, std::string(40, 'x').data(), 40));
  ws_upload_abort(&u); // the connection dropped
  TEST_ASSERT_FALSE(m.has("documents/a/" WS_TMP_NAME));
  TEST_ASSERT_EQUAL_STRING("old", m.get("documents/a/b.md").c_str());
  // fewer bytes than announced, then the end
  TEST_ASSERT_EQUAL(WS_OK, ws_upload_begin(&u, &m.fs, "documents/a/c.md", 100));
  ws_upload_chunk(&u, std::string(40, 'x').data(), 40);
  ws_put_t how;
  char fin[80];
  TEST_ASSERT_EQUAL(WS_E_SHORT, ws_upload_end(&u, &how, fin));
  TEST_ASSERT_FALSE(m.has("documents/a/c.md"));
  TEST_ASSERT_FALSE(m.has("documents/a/" WS_TMP_NAME));
  // more bytes than announced
  TEST_ASSERT_EQUAL(WS_OK, ws_upload_begin(&u, &m.fs, "documents/a/c.md", 10));
  TEST_ASSERT_EQUAL(WS_E_SHORT, ws_upload_chunk(&u, std::string(20, 'x').data(), 20));
  TEST_ASSERT_FALSE(m.has("documents/a/" WS_TMP_NAME));
}

void test_power_loss_leaves_old_file_and_cleanup_removes_temp() {
  MemFs m;
  m.put("documents/a/b.md", "old");
  m.nodes["documents"] = MemFs::Node{true, ""};
  m.nodes["documents/a"] = MemFs::Node{true, ""};
  m.nodes["settings"] = MemFs::Node{true, ""};
  m.nodes["settings/fonts"] = MemFs::Node{true, ""};
  ws_upload_t u;
  memset(&u, 0, sizeof(u));
  ws_upload_begin(&u, &m.fs, "documents/a/b.md", 100);
  ws_upload_chunk(&u, std::string(40, 'x').data(), 40);
  // power gone: no abort. Only the flash content survives.
  m.open_handles.clear();
  TEST_ASSERT_TRUE(m.has("documents/a/" WS_TMP_NAME));
  TEST_ASSERT_EQUAL_STRING("old", m.get("documents/a/b.md").c_str());
  m.put("settings/fonts/" WS_TMP_NAME, "half");
  m.put("documents/a/keep.md", "k");
  ws_store_cleanup(&m.fs);
  TEST_ASSERT_FALSE(m.has("documents/a/" WS_TMP_NAME));
  TEST_ASSERT_FALSE(m.has("settings/fonts/" WS_TMP_NAME));
  TEST_ASSERT_TRUE(m.has("documents/a/keep.md"));
  TEST_ASSERT_EQUAL_STRING("old", m.get("documents/a/b.md").c_str());
}

void test_second_upload_while_one_runs_is_busy() {
  MemFs m;
  ws_upload_t u;
  memset(&u, 0, sizeof(u));
  TEST_ASSERT_EQUAL(WS_OK, ws_upload_begin(&u, &m.fs, "documents/a/b.md", 10));
  TEST_ASSERT_EQUAL(WS_E_BUSY, ws_upload_begin(&u, &m.fs, "documents/a/c.md", 10));
  ws_upload_abort(&u);
}

void test_rename_and_delete_follow_the_rules() {
  MemFs m;
  m.put("documents/a/one.md", "1");
  m.put("documents/a/two.md", "2");
  m.put("settings/settings.json", "{}");
  m.put("settings/fonts/f.ttf", "font");
  TEST_ASSERT_EQUAL(WS_E_EXISTS, ws_store_rename(&m.fs, "documents/a/one.md", "documents/a/two.md"));
  TEST_ASSERT_EQUAL_STRING("2", m.get("documents/a/two.md").c_str());
  TEST_ASSERT_EQUAL(WS_E_NAME, ws_store_rename(&m.fs, "settings/settings.json", "documents/a/s.md"));
  TEST_ASSERT_EQUAL(WS_E_NAME, ws_store_rename(&m.fs, "documents/a/one.md", "documents/a/../x.md"));
  TEST_ASSERT_EQUAL(WS_E_NOT_FOUND, ws_store_rename(&m.fs, "documents/a/nope.md", "documents/a/x.md"));
  TEST_ASSERT_EQUAL(WS_OK, ws_store_rename(&m.fs, "documents/a/one.md", "documents/b/uno.md")); // new folder
  TEST_ASSERT_TRUE(m.has("documents/b/uno.md"));
  m.fail_rename = true;
  TEST_ASSERT_EQUAL(WS_E_IO, ws_store_rename(&m.fs, "documents/a/two.md", "documents/a/zwei.md"));
  TEST_ASSERT_TRUE(m.has("documents/a/two.md"));
  TEST_ASSERT_EQUAL(WS_E_NAME, ws_store_delete(&m.fs, "settings/settings.json"));
  TEST_ASSERT_EQUAL(WS_E_NAME, ws_store_delete(&m.fs, "settings/fonts/f.ttf"));
  TEST_ASSERT_EQUAL(WS_E_NAME, ws_store_delete(&m.fs, "documents/a"));
  TEST_ASSERT_EQUAL(WS_E_NOT_FOUND, ws_store_delete(&m.fs, "documents/a/nope.md"));
  TEST_ASSERT_EQUAL(WS_OK, ws_store_delete(&m.fs, "documents/a/two.md"));
  TEST_ASSERT_TRUE(m.has("settings/settings.json") && m.has("settings/fonts/f.ttf"));
}

static bool collect(const char *path, uint32_t, ws_kind_t, void *ctx) {
  ((std::vector<std::string> *)ctx)->push_back(path);
  return true;
}

void test_list_shows_only_layout_files() {
  MemFs m;
  m.nodes["documents"] = MemFs::Node{true, ""};
  m.nodes["documents/a"] = MemFs::Node{true, ""};
  m.nodes["documents/b"] = MemFs::Node{true, ""};
  m.put("documents/a/one.md", "1");
  m.put("documents/a/pic.png", "x");
  m.put("documents/a/" WS_TMP_NAME, "t");
  m.put("documents/b/two.txt", "2");
  m.put("documents/loose.md", "l");
  std::vector<std::string> v;
  TEST_ASSERT_EQUAL(2, ws_store_list(&m.fs, false, 100, collect, &v));
  TEST_ASSERT_EQUAL_STRING("documents/a/one.md", v[0].c_str());
  TEST_ASSERT_EQUAL_STRING("documents/b/two.txt", v[1].c_str());
  v.clear();
  TEST_ASSERT_EQUAL(1, ws_store_list(&m.fs, false, 1, collect, &v)); // capped
}

// ---- zip ----

static std::string zip_of(MemFs &m, std::vector<std::string> paths, size_t chunk, int *rc_out = nullptr) {
  std::vector<const char *> p;
  for (auto &s : paths) p.push_back(s.c_str());
  static ws_zip_entry_t entries[WS_ZIP_MAX_FILES];
  ws_zip_t z;
  TEST_ASSERT_TRUE(ws_zip_begin(&z, &m.fs, entries, p.data(), (int)p.size()));
  std::string out;
  std::vector<char> buf(chunk);
  int rc;
  while ((rc = ws_zip_read(&z, buf.data(), chunk)) > 0) out.append(buf.data(), (size_t)rc);
  ws_zip_end(&z);
  if (rc_out) *rc_out = rc;
  return out;
}

static uint32_t rd32(const std::string &s, size_t o) {
  return (uint8_t)s[o] | ((uint8_t)s[o + 1] << 8) | ((uint8_t)s[o + 2] << 16) | ((uint32_t)(uint8_t)s[o + 3] << 24);
}

void test_zip_is_valid_for_any_chunk_size() {
  MemFs m;
  m.put("documents/a/one.md", std::string(1000, 'a'));
  m.put("documents/a/empty.md", "");
  m.put("documents/b/two.txt", "hello");
  std::vector<std::string> names = {"documents/a/one.md", "documents/a/empty.md", "documents/b/two.txt"};
  std::string ref = zip_of(m, names, 4096);
  for (size_t chunk : {1u, 7u, 64u, 333u}) TEST_ASSERT_TRUE(zip_of(m, names, chunk) == ref);
  // end of central directory
  size_t eocd = ref.size() - 22;
  TEST_ASSERT_EQUAL_HEX32(0x06054b50u, rd32(ref, eocd));
  TEST_ASSERT_EQUAL_UINT32(3, (uint8_t)ref[eocd + 10]);
  uint32_t cd_off = rd32(ref, eocd + 16), cd_size = rd32(ref, eocd + 12);
  TEST_ASSERT_EQUAL_UINT32(eocd, cd_off + cd_size);
  // first central entry: crc of 1000 'a' and the offset 0
  TEST_ASSERT_EQUAL_HEX32(0x02014b50u, rd32(ref, cd_off));
  std::string a(1000, 'a');
  TEST_ASSERT_EQUAL_HEX32(trmnl_crc32(0, a.data(), a.size()), rd32(ref, cd_off + 16));
  TEST_ASSERT_EQUAL_UINT32(1000, rd32(ref, cd_off + 20));
  TEST_ASSERT_EQUAL_UINT32(0, rd32(ref, cd_off + 42));
  // the local header at the stored offset of entry 3 is a local header
  size_t e3 = cd_off + (46 + 18) + (46 + 20);
  TEST_ASSERT_EQUAL_HEX32(0x02014b50u, rd32(ref, e3));
  TEST_ASSERT_EQUAL_HEX32(0x04034b50u, rd32(ref, rd32(ref, e3 + 42)));
}

void test_zip_unreadable_file_is_an_error_not_a_short_zip() {
  MemFs m;
  m.put("documents/a/one.md", std::string(1000, 'a'));
  m.fail_read_after = 500; // the card/flash fails halfway
  int rc = 1;
  zip_of(m, {"documents/a/one.md"}, 100, &rc);
  TEST_ASSERT_TRUE(rc < 0);
  int rc2 = 1;
  MemFs m2;
  zip_of(m2, {"documents/a/gone.md"}, 100, &rc2); // listed, then deleted
  TEST_ASSERT_TRUE(rc2 < 0);
}

void test_zip_refuses_too_many_or_long() {
  MemFs m;
  ws_zip_entry_t e[WS_ZIP_MAX_FILES];
  ws_zip_t z;
  const char *p[1] = {"x"};
  TEST_ASSERT_FALSE(ws_zip_begin(&z, &m.fs, e, p, WS_ZIP_MAX_FILES + 1));
  std::string longp(80, 'a');
  const char *q[1] = {longp.c_str()};
  TEST_ASSERT_FALSE(ws_zip_begin(&z, &m.fs, e, q, 1));
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_classify_layout);
  RUN_TEST(test_bad_names_are_refused);
  RUN_TEST(test_alt_name);
  RUN_TEST(test_pin_is_six_digits_and_checked);
  RUN_TEST(test_wrong_codes_lock_then_die);
  RUN_TEST(test_sessions_are_capped);
  RUN_TEST(test_idle_timer);
  RUN_TEST(test_home_when_wifi_is_up_ap_when_not);
  RUN_TEST(test_login_token_works_once_then_needs_the_cookie);
  RUN_TEST(test_wrong_or_expired_token_is_refused_and_counts);
  RUN_TEST(test_token_guesses_lock_like_wrong_codes);
  RUN_TEST(test_firmware_never_logs_token_or_code);
  RUN_TEST(test_upload_new_file_is_atomic_and_clean);
  RUN_TEST(test_upload_never_overwrites_different_file);
  RUN_TEST(test_upload_of_same_bytes_writes_nothing);
  RUN_TEST(test_upload_refuses_names_and_sizes);
  RUN_TEST(test_upload_refused_when_it_would_fill_the_disk);
  RUN_TEST(test_full_disk_midway_leaves_old_file_and_no_temp);
  RUN_TEST(test_close_failure_means_no_file);
  RUN_TEST(test_silent_loss_at_close_is_caught);
  RUN_TEST(test_cable_pulled_midway_drops_the_temp);
  RUN_TEST(test_power_loss_leaves_old_file_and_cleanup_removes_temp);
  RUN_TEST(test_second_upload_while_one_runs_is_busy);
  RUN_TEST(test_rename_and_delete_follow_the_rules);
  RUN_TEST(test_list_shows_only_layout_files);
  RUN_TEST(test_zip_is_valid_for_any_chunk_size);
  RUN_TEST(test_zip_unreadable_file_is_an_error_not_a_short_zip);
  RUN_TEST(test_zip_refuses_too_many_or_long);
  return UNITY_END();
}
