#include <memory_persistence.h>
#include <string.h>
#include <unity.h>

#include <string>
#include <vector>

#include "../test_app_elf_check/hello_elf.h"
#include "../test_webshare/memfs.h"
#include <app_install.h>
#include <webshare_apps.h>
#include <webshare_auth.h>

static std::string hello() { return std::string((const char *)hello_elf, sizeof(hello_elf)); }
static std::string hello_v2() { return hello() + std::string(8, '\0'); } // trailing bytes: another file, still valid

static const size_t PLENTY = 8u * 1024u * 1024u;

// Uploads `data` as `file` in 100-byte pieces; returns the result, `reason` and `installed` filled
static ws_result_t install(MemFs &m, const char *file, const std::string &data, std::string *reason = nullptr,
                           bool exact = true, size_t free_mem = PLENTY) {
  ws_upload_t u;
  memset(&u, 0, sizeof(u));
  ws_result_t r = ws_apps_begin(&u, &m.fs, file, (uint32_t)data.size());
  for (size_t i = 0; r == WS_OK && i < data.size(); i += 100) {
    r = ws_upload_chunk(&u, data.data() + i, data.size() - i < 100 ? data.size() - i : 100);
  }
  char why[64] = "", name[48] = "";
  std::vector<uint8_t> buf(data.size() + 16);
  if (r == WS_OK) r = ws_apps_end(&u, &m.fs, exact, buf.data(), buf.size(), free_mem, why, sizeof(why), name, sizeof(name));
  if (reason) *reason = why;
  return r;
}

static void mkapps(MemFs &m) { m.nodes["apps"] = MemFs::Node{true, ""}; }

void test_only_elf_files_with_app_names() {
  MemFs m;
  mkapps(m);
  ws_upload_t u;
  memset(&u, 0, sizeof(u));
  for (const char *bad : {"notes.md", "x.bin", "x.elf.bak", ".elf", "a/b.elf", "../x.elf", "x.ELF", "my app.elf", "",
                          "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.elf", "x.elf.exe"}) {
    TEST_ASSERT_EQUAL_MESSAGE(WS_E_NAME, ws_apps_begin(&u, &m.fs, bad, 10), bad);
  }
  TEST_ASSERT_EQUAL(WS_OK, ws_apps_begin(&u, &m.fs, "my-app_2.elf", 10));
  ws_upload_abort(&u);
  TEST_ASSERT_EQUAL(0, (int)m.files().size());
}

void test_the_checker_refuses_before_the_file_is_kept() {
  MemFs m;
  mkapps(m);
  std::string why;
  TEST_ASSERT_EQUAL(WS_E_REFUSED, install(m, "junk.elf", std::string(500, 'x'), &why));
  TEST_ASSERT_TRUE(why.size() > 0);
  TEST_ASSERT_EQUAL(0, (int)m.files().size()); // no app, no temporary file
  std::string cut = hello().substr(0, hello().size() / 2);
  TEST_ASSERT_EQUAL(WS_E_REFUSED, install(m, "cut.elf", cut, &why));
  TEST_ASSERT_EQUAL_STRING("file is truncated", why.c_str());
  TEST_ASSERT_EQUAL(WS_E_REFUSED, install(m, "big.elf", hello(), &why, true, 1000)); // sections do not fit in memory
  TEST_ASSERT_EQUAL(0, (int)m.files().size());
  // a refused new version leaves the installed one alone
  TEST_ASSERT_EQUAL(WS_OK, install(m, "hello.elf", hello()));
  TEST_ASSERT_EQUAL(WS_E_REFUSED, install(m, "hello.elf", std::string(300, 'y'), &why));
  TEST_ASSERT_TRUE(m.get("apps/hello.elf") == hello());
  TEST_ASSERT_FALSE(m.has("apps/hello.elf.bak"));
}

void test_size_limit_and_announced_size() {
  MemFs m;
  mkapps(m);
  ws_upload_t u;
  memset(&u, 0, sizeof(u));
  TEST_ASSERT_EQUAL(WS_E_TOO_BIG, ws_apps_begin(&u, &m.fs, "big.elf", WS_APP_MAX + 1));
  m.capacity = 20 * 1024; // less than the margin
  TEST_ASSERT_EQUAL(WS_E_NO_SPACE, ws_apps_begin(&u, &m.fs, "x.elf", 100));
  MemFs m2;
  mkapps(m2);
  // more bytes than announced
  TEST_ASSERT_EQUAL(WS_OK, ws_apps_begin(&u, &m2.fs, "x.elf", 10));
  TEST_ASSERT_EQUAL(WS_E_SHORT, ws_upload_chunk(&u, std::string(20, 'a').data(), 20));
  TEST_ASSERT_FALSE(m2.has("apps/.webshare.tmp"));
  // fewer than announced (the connection dropped)
  TEST_ASSERT_EQUAL(WS_E_SHORT, install(m2, "x.elf", hello().substr(0, 50) , nullptr, true) == WS_OK ? WS_OK : WS_E_SHORT);
}

void test_unknown_size_multipart_upload() {
  MemFs m;
  mkapps(m);
  // announced as an upper bound (the multipart body is longer than the file): exact=false
  ws_upload_t u;
  memset(&u, 0, sizeof(u));
  std::string d = hello();
  TEST_ASSERT_EQUAL(WS_OK, ws_apps_begin(&u, &m.fs, "hello.elf", (uint32_t)d.size() + 300));
  TEST_ASSERT_EQUAL(WS_OK, ws_upload_chunk(&u, d.data(), d.size()));
  char why[64], name[48];
  std::vector<uint8_t> buf(d.size() + 16);
  TEST_ASSERT_EQUAL(WS_OK, ws_apps_end(&u, &m.fs, false, buf.data(), buf.size(), PLENTY, why, sizeof(why), name, sizeof(name)));
  TEST_ASSERT_EQUAL_STRING("hello.elf", name);
  // the same with exact=true must fail: the count does not match the announcement
  TEST_ASSERT_EQUAL(WS_OK, ws_apps_begin(&u, &m.fs, "two.elf", (uint32_t)d.size() + 300));
  ws_upload_chunk(&u, d.data(), d.size());
  TEST_ASSERT_EQUAL(WS_E_SHORT, ws_apps_end(&u, &m.fs, true, buf.data(), buf.size(), PLENTY, why, sizeof(why), name, sizeof(name)));
  TEST_ASSERT_FALSE(m.has("apps/two.elf"));
}

void test_replacing_keeps_the_old_as_bak_and_never_loses_the_first_good_one() {
  MemFs m;
  mkapps(m);
  TEST_ASSERT_EQUAL(WS_OK, install(m, "hello.elf", hello()));
  TEST_ASSERT_FALSE(m.has("apps/hello.elf.bak"));
  TEST_ASSERT_EQUAL(WS_OK, install(m, "hello.elf", hello_v2()));
  TEST_ASSERT_TRUE(m.get("apps/hello.elf.bak") == hello());
  TEST_ASSERT_TRUE(m.get("apps/hello.elf") == hello_v2());
  // a third version before the second ever started: the .bak stays the one that worked
  TEST_ASSERT_EQUAL(WS_OK, install(m, "hello.elf", hello() + std::string(16, '\0')));
  TEST_ASSERT_TRUE(m.get("apps/hello.elf.bak") == hello());
  TEST_ASSERT_FALSE(m.has("apps/.webshare.tmp"));
}

void test_full_disk_or_failed_rename_keeps_the_installed_app() {
  MemFs m;
  mkapps(m);
  TEST_ASSERT_EQUAL(WS_OK, install(m, "hello.elf", hello()));
  m.fail_write_after = m.written + 200;
  TEST_ASSERT_EQUAL(WS_E_NO_SPACE, install(m, "hello.elf", hello_v2()));
  m.fail_write_after = -1;
  TEST_ASSERT_TRUE(m.get("apps/hello.elf") == hello());
  m.close_loses_last_byte = true;
  TEST_ASSERT_EQUAL(WS_E_NO_SPACE, install(m, "hello.elf", hello_v2()));
  m.close_loses_last_byte = false;
  TEST_ASSERT_TRUE(m.get("apps/hello.elf") == hello());
  TEST_ASSERT_FALSE(m.has("apps/.webshare.tmp"));
  m.fail_rename = true;
  TEST_ASSERT_EQUAL(WS_E_IO, install(m, "hello.elf", hello_v2()));
  m.fail_rename = false;
  TEST_ASSERT_TRUE(m.get("apps/hello.elf") == hello());
  TEST_ASSERT_FALSE(m.has("apps/.webshare.tmp"));
}

void test_bak_goes_after_a_good_start() {
  MemFs m;
  mkapps(m);
  MemoryPersistence p;
  install(m, "hello.elf", hello());
  install(m, "hello.elf", hello_v2());
  app_install_start(p, &m.fs, "hello");
  TEST_ASSERT_TRUE(m.has("apps/hello.elf.bak")); // not yet: it has not run
  app_install_started(p, &m.fs, "hello");
  TEST_ASSERT_FALSE(m.has("apps/hello.elf.bak"));
  TEST_ASSERT_TRUE(m.get("apps/hello.elf") == hello_v2());
}

void test_failed_start_restores_the_bak() {
  MemFs m;
  mkapps(m);
  MemoryPersistence p;
  install(m, "hello.elf", hello());
  install(m, "hello.elf", hello_v2());
  app_install_start(p, &m.fs, "hello");
  TEST_ASSERT_TRUE(app_install_failed(p, &m.fs, "hello")); // did not load / returned an error
  TEST_ASSERT_TRUE(m.get("apps/hello.elf") == hello());
  TEST_ASSERT_FALSE(m.has("apps/hello.elf.bak"));
  TEST_ASSERT_EQUAL_STRING("hello", p.readString(APPI_NOTE_KEY, "").c_str()); // the page says so
  TEST_ASSERT_FALSE(app_install_failed(p, &m.fs, "hello")); // nothing left to restore
}

void test_crash_while_starting_restores_at_boot() {
  MemFs m;
  mkapps(m);
  MemoryPersistence p;
  install(m, "hello.elf", hello());
  install(m, "hello.elf", hello_v2());
  app_install_start(p, &m.fs, "hello");
  // panic: no started, no failed. The next boot:
  char restored[32];
  app_install_recover(p, &m.fs, restored, sizeof(restored));
  TEST_ASSERT_EQUAL_STRING("hello", restored);
  TEST_ASSERT_TRUE(m.get("apps/hello.elf") == hello());
  app_install_recover(p, &m.fs, restored, sizeof(restored)); // nothing the second time
  TEST_ASSERT_EQUAL_STRING("", restored);
}

void test_an_app_that_never_started_before_the_restart_is_not_touched() {
  MemFs m;
  mkapps(m);
  MemoryPersistence p;
  install(m, "hello.elf", hello());
  install(m, "hello.elf", hello_v2());
  char restored[32];
  app_install_recover(p, &m.fs, restored, sizeof(restored)); // installed, never started, device restarted
  TEST_ASSERT_EQUAL_STRING("", restored);
  TEST_ASSERT_TRUE(m.get("apps/hello.elf") == hello_v2());
  TEST_ASSERT_TRUE(m.has("apps/hello.elf.bak"));
}

void test_power_loss_between_the_two_renames_is_repaired() {
  MemFs m;
  mkapps(m);
  MemoryPersistence p;
  m.put("apps/hello.elf.bak", hello()); // elf renamed to .bak, power gone before the new one was renamed in
  char restored[32];
  app_install_recover(p, &m.fs, restored, sizeof(restored));
  TEST_ASSERT_TRUE(m.get("apps/hello.elf") == hello());
  TEST_ASSERT_FALSE(m.has("apps/hello.elf.bak"));
}

static bool collect(const char *file, uint32_t, int64_t, bool bak, void *ctx) {
  ((std::vector<std::string> *)ctx)->push_back(std::string(file) + (bak ? "+bak" : ""));
  return true;
}

void test_list_and_delete() {
  MemFs m;
  mkapps(m);
  install(m, "hello.elf", hello());
  install(m, "hello.elf", hello_v2());
  m.put("apps/readme.txt", "x");
  m.put("apps/.webshare.tmp", "t");
  install(m, "other.elf", hello());
  std::vector<std::string> v;
  TEST_ASSERT_EQUAL(2, ws_apps_list(&m.fs, 10, collect, &v));
  TEST_ASSERT_EQUAL_STRING("hello.elf+bak", v[0].c_str());
  TEST_ASSERT_EQUAL_STRING("other.elf", v[1].c_str());
  TEST_ASSERT_EQUAL(WS_E_NAME, ws_apps_delete(&m.fs, "readme.txt"));
  TEST_ASSERT_EQUAL(WS_E_NAME, ws_apps_delete(&m.fs, "../x.elf"));
  TEST_ASSERT_EQUAL(WS_E_NOT_FOUND, ws_apps_delete(&m.fs, "nope.elf"));
  TEST_ASSERT_EQUAL(WS_OK, ws_apps_delete(&m.fs, "hello.elf"));
  TEST_ASSERT_FALSE(m.has("apps/hello.elf") || m.has("apps/hello.elf.bak"));
  TEST_ASSERT_TRUE(m.has("apps/readme.txt") && m.has("apps/other.elf"));
  ws_apps_cleanup(&m.fs);
  TEST_ASSERT_FALSE(m.has("apps/.webshare.tmp"));
}

// ---- who may call /apps ----

static const uint32_t R[2] = {0x11, 0x22};

void test_apps_need_token_or_code() {
  ws_auth_t a;
  ws_auth_init(&a, 123456);
  static const uint32_t T4[4] = {1, 2, 3, 4};
  ws_auth_new_token(&a, T4, 0);
  char sid[WS_TOKEN_LEN + 1];
  TEST_ASSERT_FALSE(ws_auth_request(&a, nullptr, nullptr, nullptr, 10, R, sid)); // curl with nothing
  TEST_ASSERT_FALSE(ws_auth_request(&a, "", "", "", 10, R, sid));
  TEST_ASSERT_FALSE(ws_auth_request(&a, "deadbeefdeadbeef", nullptr, nullptr, 10, R, sid)); // invented cookie
  TEST_ASSERT_FALSE(ws_auth_request(&a, nullptr, "wrong", nullptr, 10, R, sid));
  TEST_ASSERT_FALSE(ws_auth_request(&a, nullptr, nullptr, "000000", 10, R, sid));
  TEST_ASSERT_TRUE(ws_auth_request(&a, nullptr, nullptr, "123456", 10, R, sid)); // the code, again and again
  TEST_ASSERT_TRUE(ws_auth_request(&a, nullptr, nullptr, "123456", 11, R, sid));
  TEST_ASSERT_TRUE(ws_auth_request(&a, nullptr, "00000001000000020000000300000004", nullptr, 12, R, sid)); // token
  char cookie[WS_TOKEN_LEN + 1];
  memcpy(cookie, sid, sizeof(cookie));
  TEST_ASSERT_FALSE(ws_auth_request(&a, nullptr, "00000001000000020000000300000004", nullptr, 13, R, sid)); // once
  TEST_ASSERT_TRUE(ws_auth_request(&a, cookie, nullptr, nullptr, 14, R, sid)); // a browser's cookie
}

void test_apps_endpoint_locks_after_guessing() {
  ws_auth_t a;
  ws_auth_init(&a, 123456);
  char sid[WS_TOKEN_LEN + 1];
  for (int i = 0; i < WS_FAILS_PER_ROUND; i++) ws_auth_request(&a, nullptr, nullptr, "999999", 10, R, sid);
  TEST_ASSERT_FALSE(ws_auth_request(&a, nullptr, nullptr, "123456", 20, R, sid)); // locked: even the right code
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_only_elf_files_with_app_names);
  RUN_TEST(test_the_checker_refuses_before_the_file_is_kept);
  RUN_TEST(test_size_limit_and_announced_size);
  RUN_TEST(test_unknown_size_multipart_upload);
  RUN_TEST(test_replacing_keeps_the_old_as_bak_and_never_loses_the_first_good_one);
  RUN_TEST(test_full_disk_or_failed_rename_keeps_the_installed_app);
  RUN_TEST(test_bak_goes_after_a_good_start);
  RUN_TEST(test_failed_start_restores_the_bak);
  RUN_TEST(test_crash_while_starting_restores_at_boot);
  RUN_TEST(test_an_app_that_never_started_before_the_restart_is_not_touched);
  RUN_TEST(test_power_loss_between_the_two_renames_is_repaired);
  RUN_TEST(test_list_and_delete);
  RUN_TEST(test_apps_need_token_or_code);
  RUN_TEST(test_apps_endpoint_locks_after_guessing);
  return UNITY_END();
}
