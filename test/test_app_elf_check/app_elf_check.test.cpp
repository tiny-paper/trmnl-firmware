// The app ELF checker (app_elf_check.h) against a real app, apps/hello built by apps/build.sh, and
// damaged copies of it. Regenerate the fixture after changing the app's build flags:
//   apps/build.sh hello && python3 test/test_app_elf_check/gen_fixture.py
#include <app_elf_check.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include <vector>

#include "hello_elf.h"

static const size_t PLENTY = 8 * 1024 * 1024;

typedef std::vector<uint8_t> bytes_t;

static bytes_t hello() { return bytes_t(hello_elf, hello_elf + sizeof(hello_elf)); }

static uint32_t rd32(const bytes_t &b, size_t off) {
  uint32_t v;
  memcpy(&v, &b[off], 4);
  return v;
}
static uint16_t rd16(const bytes_t &b, size_t off) {
  uint16_t v;
  memcpy(&v, &b[off], 2);
  return v;
}
static void wr32(bytes_t &b, size_t off, uint32_t v) { memcpy(&b[off], &v, 4); }
static void wr16(bytes_t &b, size_t off, uint16_t v) { memcpy(&b[off], &v, 2); }

// File offset of section header `i`
static size_t shdr(const bytes_t &b, int i) { return rd32(b, 32) + (size_t)i * 40; }

// Index of the section named `name`, or -1
static int find_section(const bytes_t &b, const char *name) {
  uint16_t shnum = rd16(b, 48), shstrndx = rd16(b, 50);
  uint32_t names = rd32(b, shdr(b, shstrndx) + 16);
  for (int i = 0; i < shnum; i++) {
    if (strcmp((const char *)&b[names + rd32(b, shdr(b, i))], name) == 0) return i;
  }
  return -1;
}

static app_elf_result_t check(const bytes_t &b, size_t free_mem = PLENTY) {
  app_info_t info;
  return app_elf_check(b.data(), b.size(), free_mem, &info);
}

void test_hello_is_accepted() {
  bytes_t b = hello();
  app_info_t info;
  TEST_ASSERT_EQUAL(APP_ELF_OK, app_elf_check(b.data(), b.size(), PLENTY, &info));
  TEST_ASSERT_EQUAL_STRING("Hello", info.name);
  TEST_ASSERT_EQUAL_UINT32(APP_DEFAULT_STACK, info.stack_size);
  TEST_ASSERT_EQUAL_UINT32(APP_API_VERSION, info.api_version);
}

void test_every_truncation_is_refused() {
  bytes_t b = hello();
  for (size_t len = 0; len < b.size(); len++) {
    bytes_t t(b.begin(), b.begin() + len);
    app_info_t info;
    app_elf_result_t r = app_elf_check(t.data(), t.size(), PLENTY, &info);
    if (r == APP_ELF_OK) {
      char msg[64];
      snprintf(msg, sizeof(msg), "accepted when cut to %zu bytes", len);
      TEST_FAIL_MESSAGE(msg);
    }
  }
  bytes_t t(b.begin(), b.begin() + 40);
  TEST_ASSERT_EQUAL(APP_ELF_TRUNCATED, check(t));
}

void test_not_elf() {
  bytes_t b = hello();
  b[1] = 'X';
  TEST_ASSERT_EQUAL(APP_ELF_NOT_ELF, check(b));
  bytes_t text = {'h', 'e', 'l', 'l', 'o', '\n'};
  TEST_ASSERT_EQUAL(APP_ELF_NOT_ELF, check(text));
}

void test_wrong_arch() {
  bytes_t b = hello();
  wr16(b, 18, 243); // EM_RISCV
  TEST_ASSERT_EQUAL(APP_ELF_WRONG_ARCH, check(b));
  b = hello();
  b[4] = 2; // 64-bit
  TEST_ASSERT_EQUAL(APP_ELF_WRONG_ARCH, check(b));
  b = hello();
  b[5] = 2; // big-endian
  TEST_ASSERT_EQUAL(APP_ELF_WRONG_ARCH, check(b));
  b = hello();
  wr16(b, 16, 1); // ET_REL: an object file, not linked
  TEST_ASSERT_EQUAL(APP_ELF_WRONG_ARCH, check(b));
}

void test_section_table_out_of_file() {
  bytes_t b = hello();
  wr32(b, 32, (uint32_t)b.size() - 8);
  TEST_ASSERT_EQUAL(APP_ELF_TRUNCATED, check(b));
  b = hello();
  wr32(b, 32, 0xfffffff0u); // would wrap around
  TEST_ASSERT_EQUAL(APP_ELF_TRUNCATED, check(b));
  b = hello();
  wr16(b, 50, rd16(b, 48)); // name table index past the table
  TEST_ASSERT_EQUAL(APP_ELF_BAD_LAYOUT, check(b));
}

void test_section_contents_out_of_file() {
  bytes_t b = hello();
  int text = find_section(b, ".text");
  wr32(b, shdr(b, text) + 20, 0x7fffffff); // size
  TEST_ASSERT_EQUAL(APP_ELF_TRUNCATED, check(b));
}

void test_bad_section_name() {
  bytes_t b = hello();
  wr32(b, shdr(b, 1), 0x00ffffff);
  TEST_ASSERT_EQUAL(APP_ELF_BAD_LAYOUT, check(b));
}

// A section starting in the 4-byte rounding of .text, where the loader would map it to code (as apps
// built before apps/build.sh aligned .rodata were)
void test_section_in_text_rounding() {
  bytes_t b = hello();
  int text = find_section(b, ".text"), rodata = find_section(b, ".rodata");
  uint32_t text_end = rd32(b, shdr(b, text) + 12) + rd32(b, shdr(b, text) + 20);
  TEST_ASSERT_NOT_EQUAL(0, text_end % 4); // the fixture's .text needs the rounding
  wr32(b, shdr(b, rodata) + 12, text_end);
  TEST_ASSERT_EQUAL(APP_ELF_BAD_LAYOUT, check(b));
}

void test_entry_outside_text() {
  bytes_t b = hello();
  wr32(b, 24, 0x10);
  TEST_ASSERT_EQUAL(APP_ELF_BAD_LAYOUT, check(b));
}

void test_no_text() {
  bytes_t b = hello();
  int text = find_section(b, ".text");
  wr32(b, shdr(b, text) + 8, 2); // flags: alloc, not executable
  TEST_ASSERT_EQUAL(APP_ELF_BAD_LAYOUT, check(b));
}

// The first relocation of .rela.dyn
static size_t first_rela(const bytes_t &b) { return rd32(b, shdr(b, find_section(b, ".rela.dyn")) + 16); }

void test_relocation_symbol_out_of_table() {
  bytes_t b = hello();
  size_t r = first_rela(b);
  wr32(b, r + 4, (rd32(b, r + 4) & 0xff) | (0x10000u << 8));
  TEST_ASSERT_EQUAL(APP_ELF_BAD_LAYOUT, check(b));
}

void test_relocation_outside_loaded_sections() {
  bytes_t b = hello();
  wr32(b, first_rela(b), 0x00100000);
  TEST_ASSERT_EQUAL(APP_ELF_BAD_LAYOUT, check(b));
}

void test_relocation_misaligned() {
  bytes_t b = hello();
  size_t r = first_rela(b);
  wr32(b, r, rd32(b, r) + 1);
  TEST_ASSERT_EQUAL(APP_ELF_BAD_LAYOUT, check(b));
}

void test_relocation_type_unsupported() {
  bytes_t b = hello();
  size_t r = first_rela(b);
  wr32(b, r + 4, (rd32(b, r + 4) & ~0xffu) | 1); // R_XTENSA_32
  TEST_ASSERT_EQUAL(APP_ELF_BAD_LAYOUT, check(b));
}

void test_relocation_symbol_table_link() {
  bytes_t b = hello();
  int rela = find_section(b, ".rela.dyn");
  wr32(b, shdr(b, rela) + 24, 99); // sh_link
  TEST_ASSERT_EQUAL(APP_ELF_BAD_LAYOUT, check(b));
}

// The app_info section's contents
static size_t info_off(const bytes_t &b) { return rd32(b, shdr(b, find_section(b, APP_INFO_SECTION)) + 16); }

void test_no_app_info() {
  bytes_t b = hello();
  uint16_t shstrndx = rd16(b, 50);
  uint32_t names = rd32(b, shdr(b, shstrndx) + 16);
  int info = find_section(b, APP_INFO_SECTION);
  b[names + rd32(b, shdr(b, info)) + 1] = 'X'; // ".Xrmnl_app_info"
  TEST_ASSERT_EQUAL(APP_ELF_NO_INFO, check(b));
}

void test_bad_app_info() {
  bytes_t b = hello();
  wr32(b, info_off(b), 0x12345678);
  TEST_ASSERT_EQUAL(APP_ELF_BAD_INFO, check(b));
  b = hello();
  memset(&b[info_off(b) + 12], 'A', 32); // name not terminated
  TEST_ASSERT_EQUAL(APP_ELF_BAD_INFO, check(b));
  b = hello();
  wr32(b, info_off(b) + 4, 0);
  TEST_ASSERT_EQUAL(APP_ELF_BAD_INFO, check(b));
}

void test_api_too_new() {
  bytes_t b = hello();
  wr32(b, info_off(b) + 4, APP_API_VERSION + 1);
  TEST_ASSERT_EQUAL(APP_ELF_API_TOO_NEW, check(b));
}

void test_stack_too_big() {
  bytes_t b = hello();
  wr32(b, info_off(b) + 8, APP_MAX_STACK + 1);
  TEST_ASSERT_EQUAL(APP_ELF_STACK_TOO_BIG, check(b));
  wr32(b, info_off(b) + 8, APP_MAX_STACK);
  TEST_ASSERT_EQUAL(APP_ELF_OK, check(b));
}

void test_too_big_for_free_memory() {
  bytes_t b = hello();
  TEST_ASSERT_EQUAL(APP_ELF_TOO_BIG, check(b, 1024));
  TEST_ASSERT_EQUAL(APP_ELF_OK, check(b, 64 * 1024));
}

// Random damage: never a crash (run under ASan, see docs/apps.md), and a result for each
void test_random_damage_is_handled() {
  bytes_t b = hello();
  uint32_t seed = 12345;
  for (int round = 0; round < 20000; round++) {
    bytes_t t = b;
    for (int n = 0; n < 1 + round % 4; n++) {
      seed = seed * 1103515245u + 12345u;
      size_t at = (seed >> 8) % t.size();
      seed = seed * 1103515245u + 12345u;
      t[at] = (uint8_t)(seed >> 16);
    }
    app_info_t info;
    app_elf_result_t r = app_elf_check(t.data(), t.size(), PLENTY, &info);
    TEST_ASSERT_TRUE(r >= APP_ELF_OK && r <= APP_ELF_TOO_BIG);
  }
}

void test_result_strings() {
  for (int r = APP_ELF_OK; r <= APP_ELF_TOO_BIG; r++) {
    TEST_ASSERT_NOT_EQUAL(0, strcmp("unknown error", app_elf_result_str((app_elf_result_t)r)));
  }
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_hello_is_accepted);
  RUN_TEST(test_every_truncation_is_refused);
  RUN_TEST(test_not_elf);
  RUN_TEST(test_wrong_arch);
  RUN_TEST(test_section_table_out_of_file);
  RUN_TEST(test_section_contents_out_of_file);
  RUN_TEST(test_bad_section_name);
  RUN_TEST(test_section_in_text_rounding);
  RUN_TEST(test_entry_outside_text);
  RUN_TEST(test_no_text);
  RUN_TEST(test_relocation_symbol_out_of_table);
  RUN_TEST(test_relocation_outside_loaded_sections);
  RUN_TEST(test_relocation_misaligned);
  RUN_TEST(test_relocation_type_unsupported);
  RUN_TEST(test_relocation_symbol_table_link);
  RUN_TEST(test_no_app_info);
  RUN_TEST(test_bad_app_info);
  RUN_TEST(test_api_too_new);
  RUN_TEST(test_stack_too_big);
  RUN_TEST(test_too_big_for_free_memory);
  RUN_TEST(test_random_damage_is_handled);
  RUN_TEST(test_result_strings);
  return UNITY_END();
}
