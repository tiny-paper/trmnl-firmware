#include <app_path.h>
#include <string.h>
#include <unity.h>

static char out[96];

static bool join(const char *rel) { return app_path_join("/appdata/hello", rel, out, sizeof(out)); }

void test_joins_relative_paths() {
  TEST_ASSERT_TRUE(join("notes.txt"));
  TEST_ASSERT_EQUAL_STRING("/appdata/hello/notes.txt", out);
  TEST_ASSERT_TRUE(join("a/b.txt"));
  TEST_ASSERT_EQUAL_STRING("/appdata/hello/a/b.txt", out);
  TEST_ASSERT_TRUE(join(""));
  TEST_ASSERT_EQUAL_STRING("/appdata/hello", out);
  TEST_ASSERT_TRUE(join(".hidden"));
}

void test_refuses_paths_out_of_the_folder() {
  TEST_ASSERT_FALSE(join(".."));
  TEST_ASSERT_FALSE(join("../other/x"));
  TEST_ASSERT_FALSE(join("a/../../x"));
  TEST_ASSERT_FALSE(join("/etc"));
  TEST_ASSERT_FALSE(join("a\\..\\b"));
  TEST_ASSERT_FALSE(join("a//b"));
  TEST_ASSERT_FALSE(join("./a"));
  TEST_ASSERT_FALSE(join("a/"));
  TEST_ASSERT_FALSE(app_path_join("/appdata/hello", nullptr, out, sizeof(out)));
}

void test_refuses_long_paths() {
  char rel[80];
  memset(rel, 'a', sizeof(rel) - 1);
  rel[sizeof(rel) - 1] = '\0';
  TEST_ASSERT_FALSE(join(rel));
  char small[20];
  TEST_ASSERT_FALSE(app_path_join("/appdata/hello", "notes.txt", small, sizeof(small)));
}

void test_app_name_from_elf_path() {
  char name[32];
  TEST_ASSERT_TRUE(app_path_name("/apps/hello.elf", name, sizeof(name)));
  TEST_ASSERT_EQUAL_STRING("hello", name);
  TEST_ASSERT_TRUE(app_path_name("/apps/my-app_2.elf", name, sizeof(name)));
  TEST_ASSERT_FALSE(app_path_name("/apps/.elf", name, sizeof(name)));
  TEST_ASSERT_FALSE(app_path_name("/apps/hello.bin", name, sizeof(name)));
  TEST_ASSERT_FALSE(app_path_name("/apps/a/b.elf", name, sizeof(name)));
  TEST_ASSERT_FALSE(app_path_name("/other/hello.elf", name, sizeof(name)));
  TEST_ASSERT_FALSE(app_path_name("/apps/../x.elf", name, sizeof(name)));
  TEST_ASSERT_FALSE(app_path_name("/apps/aaaaaaaaaaaaaaaaaaaaaaaaa.elf", name, sizeof(name)));
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_joins_relative_paths);
  RUN_TEST(test_refuses_paths_out_of_the_folder);
  RUN_TEST(test_refuses_long_paths);
  RUN_TEST(test_app_name_from_elf_path);
  return UNITY_END();
}
