#include <string.h>
#include <unity.h>

#include <string>

#include "../test_webshare/memfs.h"
#include <webshare_files.h>

// The "files" profile (an app takes its own files over the Wi-Fi page): ROMs of the Game Boy app are the first user.

static ws_files_cfg_t roms_cfg(const char *dir = "roms") {
  ws_files_cfg_t c;
  TEST_ASSERT_TRUE(ws_files_cfg_set(&c, dir, ".gb,.GBC", 4096));
  return c;
}

static ws_result_t put(MemFs &m, const ws_files_cfg_t &c, const char *name, const std::string &data, size_t declared = 0,
                       ws_put_t *how = nullptr, char *fin = nullptr) {
  ws_upload_t u;
  memset(&u, 0, sizeof(u));
  ws_result_t r = ws_files_begin(&u, &m.fs, &c, name, (uint32_t)(declared ? declared : data.size()));
  for (size_t i = 0; r == WS_OK && i < data.size(); i += 100) {
    r = ws_upload_chunk(&u, data.data() + i, data.size() - i < 100 ? data.size() - i : 100);
  }
  ws_put_t h;
  char f[WS_PATH_MAX + 1];
  if (r == WS_OK) r = ws_upload_end(&u, how ? how : &h, fin ? fin : f);
  return r;
}

void test_config_is_checked() {
  ws_files_cfg_t c;
  TEST_ASSERT_TRUE(ws_files_cfg_set(&c, "", ".gb", 1));
  TEST_ASSERT_TRUE(ws_files_cfg_set(&c, "a/b", ".gb,.gbc", WS_FILES_BYTES_MAX));
  for (const char *bad : {"..", ".", "../x", "a/../b", "/abs", "a/", "a//b", ".hidden", "a/b/c", "a:b", "a\\b",
                          "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}) {
    TEST_ASSERT_FALSE_MESSAGE(ws_files_cfg_set(&c, bad, ".gb", 10), bad);
  }
  for (const char *bad : {"", "gb", ".", ".gb,", ",.gb", ".g b", ".g/b", ".gb..", ".toolongext", ".a,.b,.c,.d,.e,.f,.g,.h,.i"}) {
    TEST_ASSERT_FALSE_MESSAGE(ws_files_cfg_set(&c, "roms", bad, 10), bad);
  }
  TEST_ASSERT_FALSE(ws_files_cfg_set(&c, "roms", ".gb", 0));
  TEST_ASSERT_FALSE(ws_files_cfg_set(&c, "roms", ".gb", WS_FILES_BYTES_MAX + 1));
  TEST_ASSERT_FALSE(ws_files_cfg_set(&c, nullptr, ".gb", 10));
}

void test_only_listed_extensions_and_good_names() {
  MemFs m;
  m.nodes["roms"] = MemFs::Node{true, ""};
  ws_files_cfg_t c = roms_cfg();
  ws_upload_t u;
  memset(&u, 0, sizeof(u));
  for (const char *bad : {"game.exe", "game", "game.gb.exe", ".gb", "..gb", "a/b.gb", "../x.gb", "..\\x.gb", "game.gb ",
                          "gam:e.gb", "", "game.gbx", "game.sav", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.gb"}) {
    TEST_ASSERT_EQUAL_MESSAGE(WS_E_NAME, ws_files_begin(&u, &m.fs, &c, bad, 10), bad);
  }
  TEST_ASSERT_EQUAL(0, (int)m.files().size());
  TEST_ASSERT_EQUAL(WS_OK, put(m, c, "Tetris.GB", "abc")); // any case
  TEST_ASSERT_EQUAL(WS_OK, put(m, c, "Zelda (EU).gbc", "abc"));
  TEST_ASSERT_TRUE(m.has("roms/Tetris.GB"));
}

void test_too_big_is_refused_before_anything_is_written() {
  MemFs m;
  ws_files_cfg_t c = roms_cfg();
  ws_upload_t u;
  memset(&u, 0, sizeof(u));
  TEST_ASSERT_EQUAL(WS_E_TOO_BIG, ws_files_begin(&u, &m.fs, &c, "big.gb", 4097));
  TEST_ASSERT_EQUAL(0, (int)m.files().size());
  TEST_ASSERT_EQUAL(WS_OK, put(m, c, "ok.gb", std::string(4096, 'x'))); // exactly the limit
  // announces less, sends more: dropped, nothing kept
  TEST_ASSERT_EQUAL(WS_E_SHORT, put(m, c, "lie.gb", std::string(300, 'x'), 100));
  TEST_ASSERT_FALSE(m.has("roms/lie.gb"));
  TEST_ASSERT_FALSE(m.has("roms/" WS_TMP_NAME));
  char why[80];
  ws_files_why(&c, WS_E_TOO_BIG, why, sizeof(why));
  TEST_ASSERT_EQUAL_STRING("file too big (largest 4 KB)", why);
  ws_files_why(&c, WS_E_NAME, why, sizeof(why));
  TEST_ASSERT_EQUAL_STRING("name or type not allowed; only .gb .gbc", why);
}

void test_path_tricks_never_leave_the_folder() {
  MemFs m;
  ws_files_cfg_t c = roms_cfg();
  m.put("secret.gb", "mine");
  m.put("roms/keep.gb", "mine");
  ws_upload_t u;
  memset(&u, 0, sizeof(u));
  for (const char *bad : {"../secret.gb", "roms/../secret.gb", "/secret.gb", "./x.gb", "a/../../x.gb"}) {
    TEST_ASSERT_EQUAL_MESSAGE(WS_E_NAME, ws_files_begin(&u, &m.fs, &c, bad, 3), bad);
    TEST_ASSERT_EQUAL_MESSAGE(WS_E_NAME, ws_files_delete(&m.fs, &c, bad), bad);
  }
  TEST_ASSERT_EQUAL_STRING("mine", m.get("secret.gb").c_str());
  TEST_ASSERT_EQUAL_STRING("mine", m.get("roms/keep.gb").c_str());
}

void test_full_disk_is_refused_up_front_and_midway() {
  MemFs m;
  ws_files_cfg_t c = roms_cfg();
  m.capacity = 10 * 1024; // less than the free-space margin
  TEST_ASSERT_EQUAL(WS_E_NO_SPACE, put(m, c, "a.gb", "x"));
  TEST_ASSERT_EQUAL(0, (int)m.files().size());
  MemFs m2;
  m2.put("roms/a.gb", "old");
  m2.fail_write_after = 150; // announced fits, the disk fills anyway
  TEST_ASSERT_EQUAL(WS_E_NO_SPACE, put(m2, c, "a.gb", std::string(400, 'n')));
  TEST_ASSERT_EQUAL_STRING("old", m2.get("roms/a.gb").c_str());
  TEST_ASSERT_FALSE(m2.has("roms/" WS_TMP_NAME));
  MemFs m3;
  m3.close_loses_last_byte = true; // the full disk shows only at close
  TEST_ASSERT_EQUAL(WS_E_NO_SPACE, put(m3, c, "a.gb", std::string(300, 'n')));
  TEST_ASSERT_EQUAL(0, (int)m3.files().size());
}

void test_same_name_is_never_overwritten() {
  MemFs m;
  ws_files_cfg_t c = roms_cfg();
  m.put("roms/Tetris.gb", "mine");
  ws_put_t how;
  char fin[WS_PATH_MAX + 1];
  TEST_ASSERT_EQUAL(WS_OK, put(m, c, "Tetris.gb", "other", 0, &how, fin));
  TEST_ASSERT_EQUAL(WS_PUT_KEPT_BOTH, how);
  TEST_ASSERT_EQUAL_STRING("roms/Tetris (web 1).gb", fin);
  TEST_ASSERT_EQUAL_STRING("mine", m.get("roms/Tetris.gb").c_str());
  TEST_ASSERT_EQUAL_STRING("other", m.get(fin).c_str());
  TEST_ASSERT_EQUAL(WS_OK, put(m, c, "Tetris.gb", "mine", 0, &how, fin)); // same bytes: nothing written
  TEST_ASSERT_EQUAL(WS_PUT_SAME, how);
  TEST_ASSERT_EQUAL(2, (int)m.files().size());
}

void test_the_app_folder_itself_works() {
  MemFs m;
  ws_files_cfg_t c = roms_cfg("");
  m.put("Tetris.gb", "mine");
  m.put("Tetris.gb.sav", "save"); // the app's own file: not listed, not touched
  ws_put_t how;
  char fin[WS_PATH_MAX + 1];
  TEST_ASSERT_EQUAL(WS_OK, put(m, c, "Tetris.gb", "other", 0, &how, fin));
  TEST_ASSERT_EQUAL_STRING("Tetris (web 1).gb", fin);
  TEST_ASSERT_FALSE(m.has(WS_TMP_NAME));
  TEST_ASSERT_EQUAL_STRING("save", m.get("Tetris.gb.sav").c_str());
  TEST_ASSERT_EQUAL(WS_E_NAME, ws_files_delete(&m.fs, &c, "Tetris.gb.sav"));
}

typedef struct { std::string seen; } seen_t;

void test_list_delete_and_cleanup() {
  MemFs m;
  ws_files_cfg_t c = roms_cfg();
  m.nodes["roms"] = MemFs::Node{true, ""};
  m.put("roms/a.gb", "1");
  m.put("roms/b.GBC", "22");
  m.put("roms/c.sav", "3");
  m.put("roms/" WS_TMP_NAME, "half");
  m.nodes["roms/d.gb"] = MemFs::Node{true, ""}; // a folder named like a ROM
  seen_t s;
  int n = ws_files_list(&m.fs, &c, 10, [](const char *name, uint32_t size, void *ctx) {
    ((seen_t *)ctx)->seen += std::string(name) + ":" + std::to_string(size) + ";";
    return true;
  }, &s);
  TEST_ASSERT_EQUAL(2, n);
  TEST_ASSERT_EQUAL_STRING("a.gb:1;b.GBC:2;", s.seen.c_str());
  TEST_ASSERT_EQUAL(1, ws_files_list(&m.fs, &c, 1, [](const char *, uint32_t, void *) { return true; }, &s)); // bounded
  TEST_ASSERT_EQUAL(WS_E_NAME, ws_files_delete(&m.fs, &c, "c.sav")); // not a ROM: stays
  TEST_ASSERT_EQUAL(WS_E_NAME, ws_files_delete(&m.fs, &c, WS_TMP_NAME));
  TEST_ASSERT_EQUAL(WS_E_NAME, ws_files_delete(&m.fs, &c, "d.gb")); // a folder
  TEST_ASSERT_TRUE(m.has("roms/d.gb"));
  TEST_ASSERT_EQUAL(WS_E_NOT_FOUND, ws_files_delete(&m.fs, &c, "zz.gb"));
  TEST_ASSERT_EQUAL(WS_OK, ws_files_delete(&m.fs, &c, "a.gb"));
  TEST_ASSERT_FALSE(m.has("roms/a.gb"));
  ws_files_cleanup(&m.fs, &c); // a power loss left the temp file
  TEST_ASSERT_FALSE(m.has("roms/" WS_TMP_NAME));
  TEST_ASSERT_TRUE(m.has("roms/c.sav"));
}

void test_cable_pulled_and_second_upload() {
  MemFs m;
  ws_files_cfg_t c = roms_cfg();
  ws_upload_t u;
  memset(&u, 0, sizeof(u));
  TEST_ASSERT_EQUAL(WS_OK, ws_files_begin(&u, &m.fs, &c, "a.gb", 500));
  ws_upload_chunk(&u, "abc", 3);
  ws_upload_t u2 = u;
  TEST_ASSERT_EQUAL(WS_E_BUSY, ws_files_begin(&u2, &m.fs, &c, "b.gb", 5));
  ws_upload_abort(&u);
  TEST_ASSERT_EQUAL(0, (int)m.files().size());
  TEST_ASSERT_EQUAL(0, (int)m.open_handles.size());
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_config_is_checked);
  RUN_TEST(test_only_listed_extensions_and_good_names);
  RUN_TEST(test_too_big_is_refused_before_anything_is_written);
  RUN_TEST(test_path_tricks_never_leave_the_folder);
  RUN_TEST(test_full_disk_is_refused_up_front_and_midway);
  RUN_TEST(test_same_name_is_never_overwritten);
  RUN_TEST(test_the_app_folder_itself_works);
  RUN_TEST(test_list_delete_and_cleanup);
  RUN_TEST(test_cable_pulled_and_second_upload);
  return UNITY_END();
}
