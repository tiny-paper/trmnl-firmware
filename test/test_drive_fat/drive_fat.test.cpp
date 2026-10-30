// The USB drive's FAT16 image: built, walked back, and refused when damaged (drive_fat.h).
// With dosfstools and mtools installed, real FAT code checks our image and writes into it.
#include <drive_fat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <unity.h>

#include <string>
#include <vector>

#define IMG_SIZE (4u * 1024 * 1024)

static uint8_t *img;
static uint8_t *scratch;
static size_t scratch_len;

struct Walked {
  std::vector<drive_fat_entry_t> entries;
};

static bool collect(void *ctx, const drive_fat_entry_t *e) {
  ((Walked *)ctx)->entries.push_back(*e);
  return true;
}

static int walk(Walked &w) { return drive_fat_walk(img, IMG_SIZE, scratch, scratch_len, collect, &w); }

static const drive_fat_entry_t *find(Walked &w, const char *path) {
  for (auto &e : w.entries) {
    if (strcmp(e.path, path) == 0) return &e;
  }
  return nullptr;
}

static std::string content(const drive_fat_entry_t *e) {
  std::string s(e->size, '\0');
  int n = drive_fat_read(img, IMG_SIZE, e, 0, (uint8_t *)&s[0], e->size);
  TEST_ASSERT_EQUAL((int)e->size, n);
  return s;
}

static std::vector<std::string> bodies;

static bool fill(void *ctx, size_t index, uint8_t *dst, uint32_t len) {
  (void)ctx;
  memcpy(dst, bodies[index].data(), len);
  return true;
}

static int build(const std::vector<std::pair<const char *, std::string>> &in) {
  std::vector<drive_fat_file_t> files;
  bodies.clear();
  for (auto &f : in) bodies.push_back(f.second);
  for (size_t i = 0; i < in.size(); i++) files.push_back({in[i].first, (uint32_t)bodies[i].size()});
  return drive_fat_build(img, IMG_SIZE, "TRMNL", files.data(), files.size(), fill, nullptr,
                         drive_fat_time(2026, 10, 30, 12, 0, 0));
}

static void build_sample(void) {
  std::string big(70000, 'x');
  for (size_t i = 0; i < big.size(); i++) big[i] = (char)('a' + i % 26);
  TEST_ASSERT_EQUAL(DRIVE_FAT_OK, build({{"documents/Notes/Hello world.md", "# Hi\n"},
                                         {"documents/Notes/empty.txt", ""},
                                         {"documents/Reisen/Café über.md", "unicode\n"},
                                         {"documents/Notes/long one.md", big},
                                         {"settings/settings.json", "{}"}}));
}

void test_round_trip(void) {
  build_sample();
  Walked w;
  TEST_ASSERT_EQUAL(DRIVE_FAT_OK, walk(w));
  TEST_ASSERT_NOT_NULL(find(w, "documents"));
  TEST_ASSERT_TRUE(find(w, "documents/Notes")->is_dir);
  TEST_ASSERT_EQUAL_STRING("# Hi\n", content(find(w, "documents/Notes/Hello world.md")).c_str());
  TEST_ASSERT_EQUAL(0, find(w, "documents/Notes/empty.txt")->size);
  TEST_ASSERT_EQUAL_STRING("unicode\n", content(find(w, "documents/Reisen/Café über.md")).c_str());
  TEST_ASSERT_TRUE(content(find(w, "documents/Notes/long one.md")) == bodies[3]);
  TEST_ASSERT_EQUAL_STRING("{}", content(find(w, "settings/settings.json")).c_str());
  // A read from the middle crosses clusters
  uint8_t part[10];
  TEST_ASSERT_EQUAL(10, drive_fat_read(img, IMG_SIZE, find(w, "documents/Notes/long one.md"), 1020, part, 10));
  TEST_ASSERT_EQUAL_MEMORY(bodies[3].data() + 1020, part, 10);
}

void test_refuses_what_it_cannot_build(void) {
  TEST_ASSERT_EQUAL(DRIVE_FAT_EINVAL, build({{"documents/a/b?.md", "x"}}));
  TEST_ASSERT_EQUAL(DRIVE_FAT_EINVAL, build({{"documents/a/B.md", "x"}, {"documents/a/b.md", "y"}}));
  TEST_ASSERT_EQUAL(DRIVE_FAT_EINVAL, build({{"documents/a", "x"}, {"documents/a/b.md", "y"}}));
  std::string huge(IMG_SIZE, 'x');
  TEST_ASSERT_EQUAL(DRIVE_FAT_EFULL, build({{"documents/a/b.md", huge}}));
  drive_fat_file_t f = {"a.md", 1};
  TEST_ASSERT_EQUAL(DRIVE_FAT_EINVAL, drive_fat_build(img, 1024 * 1024, "T", &f, 1, fill, nullptr, 0));
}

void test_fill_failure_is_an_error(void) {
  drive_fat_file_t f = {"a.md", 3};
  auto fail = [](void *, size_t, uint8_t *, uint32_t) { return false; };
  TEST_ASSERT_EQUAL(DRIVE_FAT_EINVAL, drive_fat_build(img, IMG_SIZE, "T", &f, 1, fail, nullptr, 0));
}

// ---- damage ----

static uint8_t *fat1(void) { return img + 512; }
static uint16_t first_cluster_of(const char *path) {
  Walked w;
  TEST_ASSERT_EQUAL(DRIVE_FAT_OK, walk(w));
  return (uint16_t)find(w, path)->first_cluster;
}

void test_refuses_a_reformatted_or_damaged_boot_sector(void) {
  build_sample();
  Walked w;
  img[510] = 0;
  TEST_ASSERT_EQUAL(DRIVE_FAT_EFORMAT, walk(w));
  build_sample();
  img[13] = 3; // sectors per cluster not a power of two
  TEST_ASSERT_EQUAL(DRIVE_FAT_EFORMAT, walk(w));
  build_sample();
  img[19] = 0xff; // more sectors than the image
  img[20] = 0xff;
  img[32] = 0xff;
  img[35] = 0x7f;
  img[19] = 0;
  img[20] = 0;
  TEST_ASSERT_EQUAL(DRIVE_FAT_EFORMAT, walk(w));
  TEST_ASSERT_EQUAL(0, (int)w.entries.size());
}

void test_refuses_a_looping_chain(void) {
  build_sample();
  uint16_t c = first_cluster_of("documents/Notes/long one.md");
  fat1()[(c + 3) * 2] = (uint8_t)c; // the 4th cluster points back to the first
  fat1()[(c + 3) * 2 + 1] = (uint8_t)(c >> 8);
  Walked w;
  TEST_ASSERT_EQUAL(DRIVE_FAT_ECHAIN, walk(w));
  TEST_ASSERT_EQUAL(0, (int)w.entries.size());
}

void test_refuses_a_chain_shorter_than_the_size(void) {
  // A copy cut short: the entry has its size, the chain does not
  build_sample();
  uint16_t c = first_cluster_of("documents/Notes/long one.md");
  fat1()[(c + 3) * 2] = 0xff;
  fat1()[(c + 3) * 2 + 1] = 0xff;
  Walked w;
  TEST_ASSERT_EQUAL(DRIVE_FAT_ECHAIN, walk(w));
}

void test_refuses_shared_clusters(void) {
  build_sample();
  uint16_t a = first_cluster_of("documents/Notes/Hello world.md");
  uint16_t b = first_cluster_of("settings/settings.json");
  // point settings.json's entry at Hello world's cluster
  for (size_t off = 0; off + 32 <= IMG_SIZE; off += 32) {
    if (img[off + 11] != 0x0f && img[off + 26] == (uint8_t)b && img[off + 27] == (uint8_t)(b >> 8) &&
        img[off + 28] == 2 && img[off + 29] == 0) {
      img[off + 26] = (uint8_t)a;
      img[off + 27] = (uint8_t)(a >> 8);
      break;
    }
  }
  Walked w;
  TEST_ASSERT_EQUAL(DRIVE_FAT_ECHAIN, walk(w));
}

void test_refuses_out_of_range_clusters(void) {
  build_sample();
  uint16_t c = first_cluster_of("documents/Notes/long one.md");
  fat1()[c * 2] = 0xf0; // 0xfff0: reserved, not a cluster
  fat1()[c * 2 + 1] = 0xff;
  Walked w;
  TEST_ASSERT_EQUAL(DRIVE_FAT_ECHAIN, walk(w));
  // A one-cluster file whose entry names a cluster past the volume, where the bytes read as its
  // FAT entry say "end": only the range check refuses it (else a read past the image)
  build_sample();
  uint16_t a = first_cluster_of("documents/Notes/Hello world.md");
  const uint16_t far = 8150; // past the 8095 clusters, inside the FAT sectors and the bitmap
  fat1()[far * 2] = 0xff;
  fat1()[far * 2 + 1] = 0xff;
  for (size_t off = 0; off + 32 <= IMG_SIZE; off += 32) {
    if (img[off + 11] == 0x20 && img[off + 26] == (uint8_t)a && img[off + 27] == (uint8_t)(a >> 8)) {
      img[off + 26] = (uint8_t)far;
      img[off + 27] = (uint8_t)(far >> 8);
      break;
    }
  }
  w.entries.clear();
  TEST_ASSERT_EQUAL(DRIVE_FAT_ECHAIN, walk(w));
}

void test_random_damage_never_crashes(void) {
  srand(7);
  int refused = 0;
  for (int round = 0; round < 300; round++) {
    build_sample();
    int flips = 1 + rand() % 8;
    for (int i = 0; i < flips; i++) {
      // the metadata: boot sector, FATs, root and the first data clusters
      size_t off = (size_t)(rand() % (64 * 1024));
      img[off] ^= (uint8_t)(1 << (rand() % 8));
    }
    Walked w;
    int rc = walk(w);
    if (rc < 0) {
      refused++;
      TEST_ASSERT_EQUAL(0, (int)w.entries.size());
      continue;
    }
    uint8_t buf[512];
    for (auto &e : w.entries) {
      if (!e.is_dir) TEST_ASSERT_TRUE(drive_fat_read(img, IMG_SIZE, &e, 0, buf, sizeof(buf)) >= 0);
    }
  }
  TEST_ASSERT_TRUE(refused > 0);
}

// ---- what other FAT code writes ----

// A short-name-only entry, as a computer may write it: "README  TXT", lowercase base flag
void test_reads_short_names_and_skips_deleted(void) {
  TEST_ASSERT_EQUAL(DRIVE_FAT_OK, build({}));
  uint8_t *root = nullptr;
  uint32_t fat_sectors = (uint32_t)(img[22] | img[23] << 8);
  root = img + (1 + 2 * fat_sectors) * 512;
  uint8_t *e = root + 32; // after the label
  memcpy(e, "README  TXT", 11);
  e[11] = 0x20;
  e[12] = 0x08; // base in lower case
  memcpy(e + 32, "\xe5" "ONE    TXT", 11); // deleted
  e[32 + 11] = 0x20;
  Walked w;
  TEST_ASSERT_EQUAL(DRIVE_FAT_OK, walk(w));
  TEST_ASSERT_EQUAL(1, (int)w.entries.size());
  TEST_ASSERT_EQUAL_STRING("readme.TXT", w.entries[0].path);
}

void test_long_name_with_a_wrong_checksum_falls_back_to_the_short_name(void) {
  TEST_ASSERT_EQUAL(DRIVE_FAT_OK, build({{"Hello.md", "x"}}));
  uint32_t fat_sectors = (uint32_t)(img[22] | img[23] << 8);
  uint8_t *root = img + (1 + 2 * fat_sectors) * 512;
  TEST_ASSERT_EQUAL_HEX8(0x0f, root[32 + 11]);
  root[32 + 13] ^= 1;
  Walked w;
  TEST_ASSERT_EQUAL(DRIVE_FAT_OK, walk(w));
  TEST_ASSERT_EQUAL_STRING("TP000000.MD", w.entries[0].path);
}

static bool have(const char *tool) {
  char cmd[96];
  snprintf(cmd, sizeof(cmd), "command -v %s >/dev/null 2>&1", tool);
  return system(cmd) == 0;
}

static std::string run(const std::string &cmd) {
  std::string out;
  FILE *p = popen((cmd + " 2>&1").c_str(), "r");
  char line[256];
  while (p && fgets(line, sizeof(line), p)) out += line;
  int rc = p ? pclose(p) : -1;
  if (rc != 0) out += "\nexit " + std::to_string(rc);
  return out;
}

// dosfstools checks our image; mtools (another FAT implementation) writes into it like a computer
void test_other_fat_code_accepts_our_image_and_we_read_its_writes(void) {
  if (!have("fsck.fat") || !have("mcopy")) TEST_IGNORE_MESSAGE("dosfstools and mtools not installed");
  build_sample();
  char path[] = "/tmp/drive_fat_testXXXXXX";
  int fd = mkstemp(path);
  TEST_ASSERT_TRUE(fd >= 0);
  TEST_ASSERT_EQUAL((int)IMG_SIZE, (int)write(fd, img, IMG_SIZE));
  close(fd);
  std::string img_path = path;
  std::string out = run("fsck.fat -n -v " + img_path);
  TEST_ASSERT_TRUE_MESSAGE(out.find("exit") == std::string::npos, out.c_str());
  std::string listing = run("MTOOLS_SKIP_CHECK=1 mdir -i " + img_path + " -/ -b ::");
  TEST_ASSERT_TRUE_MESSAGE(listing.find("Café über.md") != std::string::npos, listing.c_str());
  std::string got = run("MTOOLS_SKIP_CHECK=1 mtype -i " + img_path + " '::/documents/Notes/Hello world.md'");
  TEST_ASSERT_EQUAL_STRING("# Hi\n", got.c_str());

  // The computer edits one, adds one with a long name, deletes one, makes a folder
  char local[] = "/tmp/drive_fat_srcXXXXXX";
  fd = mkstemp(local);
  std::string body(5000, 'k');
  TEST_ASSERT_EQUAL((int)body.size(), (int)write(fd, body.data(), body.size()));
  close(fd);
  std::string m = "MTOOLS_SKIP_CHECK=1 ";
  std::string l = local;
  TEST_ASSERT_EQUAL_STRING("", run(m + "mcopy -o -i " + img_path + " " + l + " '::/documents/Notes/Hello world.md'").c_str());
  TEST_ASSERT_EQUAL_STRING("", run(m + "mcopy -i " + img_path + " " + l + " '::/documents/Notes/A rather long title from a laptop.md'").c_str());
  TEST_ASSERT_EQUAL_STRING("", run(m + "mdel -i " + img_path + " '::/documents/Notes/empty.txt'").c_str());
  TEST_ASSERT_EQUAL_STRING("", run(m + "mmd -i " + img_path + " '::/documents/New folder'").c_str());
  out = run("fsck.fat -n " + img_path);
  TEST_ASSERT_TRUE_MESSAGE(out.find("exit") == std::string::npos, out.c_str());

  fd = open(path, 0);
  TEST_ASSERT_EQUAL((int)IMG_SIZE, (int)read(fd, img, IMG_SIZE));
  close(fd);
  unlink(path);
  unlink(local);
  Walked w;
  TEST_ASSERT_EQUAL(DRIVE_FAT_OK, walk(w));
  TEST_ASSERT_TRUE(content(find(w, "documents/Notes/Hello world.md")) == body);
  TEST_ASSERT_TRUE(content(find(w, "documents/Notes/A rather long title from a laptop.md")) == body);
  TEST_ASSERT_NULL(find(w, "documents/Notes/empty.txt"));
  TEST_ASSERT_TRUE(find(w, "documents/New folder")->is_dir);
  TEST_ASSERT_TRUE(content(find(w, "documents/Notes/long one.md")) == bodies[3]);
}

void setUp(void) {
  if (!img) {
    img = (uint8_t *)malloc(IMG_SIZE);
    scratch_len = drive_fat_scratch_size(IMG_SIZE);
    scratch = (uint8_t *)malloc(scratch_len);
  }
}
void tearDown(void) {}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_round_trip);
  RUN_TEST(test_refuses_what_it_cannot_build);
  RUN_TEST(test_fill_failure_is_an_error);
  RUN_TEST(test_refuses_a_reformatted_or_damaged_boot_sector);
  RUN_TEST(test_refuses_a_looping_chain);
  RUN_TEST(test_refuses_a_chain_shorter_than_the_size);
  RUN_TEST(test_refuses_shared_clusters);
  RUN_TEST(test_refuses_out_of_range_clusters);
  RUN_TEST(test_random_damage_never_crashes);
  RUN_TEST(test_reads_short_names_and_skips_deleted);
  RUN_TEST(test_long_name_with_a_wrong_checksum_falls_back_to_the_short_name);
  RUN_TEST(test_other_fat_code_accepts_our_image_and_we_read_its_writes);
  return UNITY_END();
}
