// Which paths the USB drive moves, and the names of conflict copies (drive_layout.h)
#include <drive_layout.h>
#include <string.h>
#include <unity.h>

void test_layout_paths(void) {
  TEST_ASSERT_TRUE(drive_layout_ok("documents/Notes/Hello.md"));
  TEST_ASSERT_TRUE(drive_layout_ok("documents/Notes/Hello.txt"));
  TEST_ASSERT_TRUE(drive_layout_ok("documents/Reisen/Café über.md"));
  TEST_ASSERT_TRUE(drive_layout_ok("settings/settings.json"));
  TEST_ASSERT_TRUE(drive_layout_ok("settings/fonts/Inter.ttf"));
  // not the layout: left alone
  TEST_ASSERT_FALSE(drive_layout_ok("documents/Hello.md"));
  TEST_ASSERT_FALSE(drive_layout_ok("documents/a/b/c.md"));
  TEST_ASSERT_FALSE(drive_layout_ok("documents/Notes/Hello.doc"));
  TEST_ASSERT_FALSE(drive_layout_ok("documents/Notes/.md"));
  TEST_ASSERT_FALSE(drive_layout_ok("settings/other.json"));
  TEST_ASSERT_FALSE(drive_layout_ok("settings/fonts/sub/x.ttf"));
  TEST_ASSERT_FALSE(drive_layout_ok("notes.md"));
  TEST_ASSERT_FALSE(drive_layout_ok(".drive/plan"));
  TEST_ASSERT_FALSE(drive_layout_ok(""));
  TEST_ASSERT_FALSE(drive_layout_ok(nullptr));
}

void test_bad_names(void) {
  // the computer's own files
  TEST_ASSERT_FALSE(drive_layout_ok("documents/Notes/._Hello.md"));
  TEST_ASSERT_FALSE(drive_layout_ok("documents/.Trashes/x.md"));
  // names FAT or a later copy cannot hold
  TEST_ASSERT_FALSE(drive_layout_ok("documents/Notes/What?.md"));
  TEST_ASSERT_FALSE(drive_layout_ok("documents/Notes/a:b.md"));
  TEST_ASSERT_FALSE(drive_layout_ok("documents/Notes/tab\there.md"));
  TEST_ASSERT_FALSE(drive_layout_ok("documents/Notes /x.md"));
  TEST_ASSERT_FALSE(drive_layout_ok("documents/Notes./x.md"));
  TEST_ASSERT_FALSE(drive_layout_ok("documents/Notes/\xff\xfe.md")); // not UTF-8
  TEST_ASSERT_FALSE(drive_layout_ok("documents/Notes/\xc0\xaf.md")); // overlong
  TEST_ASSERT_FALSE(drive_layout_ok("documents/Notes/\xed\xa0\x80.md")); // a surrogate
  TEST_ASSERT_FALSE(drive_layout_ok("documents//x.md"));
  TEST_ASSERT_FALSE(drive_layout_ok("documents/../x.md"));
  // long: 64 bytes in all is the most
  char p[80] = "documents/n/";
  size_t len = strlen(p);
  memset(p + len, 'a', 64 - len - 3);
  strcpy(p + 64 - 3, ".md");
  TEST_ASSERT_EQUAL(64, (int)strlen(p));
  TEST_ASSERT_TRUE(drive_layout_ok(p));
  p[len] = 'b';
  memmove(p + len + 1, p + len, strlen(p + len) + 1);
  TEST_ASSERT_FALSE(drive_layout_ok(p));
}

static const char *existing[4];
static bool exists(void *ctx, const char *path) {
  (void)ctx;
  for (const char *e : existing) {
    if (e && strcmp(e, path) == 0) return true;
  }
  return false;
}

void test_conflict_names(void) {
  char out[80];
  memset(existing, 0, sizeof(existing));
  TEST_ASSERT_TRUE(drive_conflict_name("documents/a/b.md", exists, nullptr, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("documents/a/b (computer).md", out);
  existing[0] = "documents/a/b (computer).md";
  existing[1] = "documents/a/b (computer 2).md";
  TEST_ASSERT_TRUE(drive_conflict_name("documents/a/b.md", exists, nullptr, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("documents/a/b (computer 3).md", out);
  TEST_ASSERT_TRUE(drive_conflict_name("settings/settings.json", exists, nullptr, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("settings/settings (computer).json", out);
  TEST_ASSERT_TRUE(drive_layout_ok(out) == false); // a settings copy is not read as settings
  // no room for the tag
  char p[80] = "documents/n/";
  size_t len = strlen(p);
  memset(p + len, 'a', 64 - len - 3);
  strcpy(p + 64 - 3, ".md");
  TEST_ASSERT_FALSE(drive_conflict_name(p, exists, nullptr, out, sizeof(out)));
}

void test_fat_case(void) {
  TEST_ASSERT_TRUE(drive_same_name_on_fat("documents/A/b.md", "documents/a/B.md"));
  TEST_ASSERT_FALSE(drive_same_name_on_fat("documents/a/b.md", "documents/a/c.md"));
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_layout_paths);
  RUN_TEST(test_bad_names);
  RUN_TEST(test_conflict_names);
  RUN_TEST(test_fat_case);
  return UNITY_END();
}
