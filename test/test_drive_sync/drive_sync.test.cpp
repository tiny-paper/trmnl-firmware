// The writer's files to the USB drive's image and back (drive_sync.h), on a fake file system that
// can run full or lose power at any write: no silent overwrite, both kept on a conflict, files
// outside the layout left alone, an import whole or not at all.
#include <drive_sync.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#define IMG_SIZE (4u * 1024 * 1024)

// ---- a fake app folder ----

struct FakeFs {
  std::map<std::string, std::string> files;
  std::set<std::string> dirs;
  int64_t capacity = 1 << 30;
  int ops = 0; // changing calls so far
  int cut_at = -1; // the changing call at which the power goes (it and all after fail)
  bool cut = false;

  int64_t used() const {
    int64_t n = 0;
    for (auto &f : files) n += (int64_t)f.second.size();
    return n;
  }
  bool change() {
    if (cut) return false;
    if (ops++ == cut_at) {
      cut = true;
      return false;
    }
    return true;
  }
  bool parent_ok(const std::string &p) const {
    size_t slash = p.rfind('/');
    return slash == std::string::npos || dirs.count(p.substr(0, slash));
  }
};

static FakeFs fs;

static int64_t f_size(void *, const char *path) {
  auto it = fs.files.find(path);
  return it == fs.files.end() ? -1 : (int64_t)it->second.size();
}
static int f_read(void *, const char *path, uint32_t off, uint8_t *buf, uint32_t len) {
  auto it = fs.files.find(path);
  if (it == fs.files.end()) return -2;
  if (off >= it->second.size()) return 0;
  uint32_t n = (uint32_t)std::min<size_t>(len, it->second.size() - off);
  memcpy(buf, it->second.data() + off, n);
  return (int)n;
}
// Like the device's: open, write, close; a cut call leaves the file as it was (LittleFS commits on close)
static int f_write(void *, const char *path, const uint8_t *buf, uint32_t len, bool append) {
  if (!fs.change() || !fs.parent_ok(path)) return -4;
  std::string next = append && fs.files.count(path) ? fs.files[path] : "";
  next.append((const char *)buf, len);
  int64_t old = fs.files.count(path) ? (int64_t)fs.files[path].size() : 0;
  if (fs.used() - old + (int64_t)next.size() > fs.capacity) return -4;
  fs.files[path] = next;
  return 0;
}
static int f_rename(void *, const char *from, const char *to) {
  if (!fs.change() || !fs.files.count(from) || !fs.parent_ok(to)) return -4;
  fs.files[to] = fs.files[from];
  fs.files.erase(from);
  return 0;
}
static int f_remove(void *, const char *path) {
  if (!fs.change()) return -4;
  if (fs.files.erase(path)) return 0;
  std::string prefix = std::string(path) + "/";
  for (auto &f : fs.files) {
    if (f.first.compare(0, prefix.size(), prefix) == 0) return -4; // not empty
  }
  return fs.dirs.erase(path) ? 0 : -2;
}
static int f_mkdir(void *, const char *path) {
  if (fs.dirs.count(path)) return 0;
  if (!fs.change() || !fs.parent_ok(path)) return -4;
  fs.dirs.insert(path);
  return 0;
}
static int f_list(void *, const char *dir, drive_list_fn cb, void *cbctx) {
  if (!fs.dirs.count(dir)) return -2;
  std::string prefix = std::string(dir) + "/";
  std::vector<std::pair<std::string, bool>> found;
  for (auto &d : fs.dirs) {
    if (d.compare(0, prefix.size(), prefix) == 0 && d.find('/', prefix.size()) == std::string::npos) {
      found.push_back({d.substr(prefix.size()), true});
    }
  }
  for (auto &f : fs.files) {
    if (f.first.compare(0, prefix.size(), prefix) == 0 && f.first.find('/', prefix.size()) == std::string::npos) {
      found.push_back({f.first.substr(prefix.size()), false});
    }
  }
  for (auto &e : found) {
    if (!cb(cbctx, e.first.c_str(), e.second, e.second ? 0 : (uint32_t)fs.files[prefix + e.first].size())) break;
  }
  return (int)found.size();
}
static int64_t f_free(void *) { return fs.capacity - fs.used(); }

static const drive_fs_t ops = {nullptr, f_size, f_read, f_write, f_rename, f_remove, f_mkdir, f_list, f_free};

static void put(const std::string &path, const std::string &body) {
  for (size_t s = path.find('/'); s != std::string::npos; s = path.find('/', s + 1)) fs.dirs.insert(path.substr(0, s));
  fs.files[path] = body;
}

// ---- the drive ----

static uint8_t *img;
static drive_manifest_entry_t entries[DRIVE_MAX_FILES];
static drive_manifest_t manifest = {entries, 0};

static void writer_folder(void) {
  fs = FakeFs();
  put("documents/Notes/Hello.md", "hello from the X\n");
  put("documents/Notes/Shopping.txt", "milk\n");
  put("documents/Story/Chapter 1.md", std::string(9000, 's'));
  put("settings/settings.json", "{\"font\":\"Inter\"}");
  put("settings/fonts/Inter.ttf", std::string(3000, 'f'));
  put("apps-private.bin", "not the writer's layout");
  put("documents/Notes/draft.doc", "not a writer type");
}

static drive_export_result_t show(void) {
  drive_export_result_t r;
  TEST_ASSERT_EQUAL(DRIVE_OK, drive_export(&ops, img, IMG_SIZE, &manifest, 0, &r));
  return r;
}

// What the computer does, on the image: a FAT library of our own would do; the reader is tested
// against mtools in test_drive_fat, so here the image is rebuilt with the computer's file set.
static std::map<std::string, std::string> computer;

static bool keep_all(void *ctx, const drive_fat_entry_t *e) {
  if (!e->is_dir) {
    std::string body(e->size, '\0');
    drive_fat_read(img, IMG_SIZE, e, 0, (uint8_t *)&body[0], e->size);
    (*(std::map<std::string, std::string> *)ctx)[e->path] = body;
  }
  return true;
}

static void computer_mounts(void) {
  computer.clear();
  static uint8_t scratch[4096];
  TEST_ASSERT_EQUAL(DRIVE_FAT_OK, drive_fat_walk(img, IMG_SIZE, scratch, sizeof(scratch), keep_all, &computer));
}

static std::vector<std::string> bodies;
static bool fill(void *, size_t i, uint8_t *dst, uint32_t len) {
  memcpy(dst, bodies[i].data(), len);
  return true;
}

static void computer_ejects(void) {
  std::vector<drive_fat_file_t> files;
  bodies.clear();
  for (auto &f : computer) bodies.push_back(f.second);
  size_t i = 0;
  for (auto &f : computer) files.push_back({f.first.c_str(), (uint32_t)bodies[i++].size()});
  TEST_ASSERT_EQUAL(DRIVE_FAT_OK, drive_fat_build(img, IMG_SIZE, "TRMNL", files.data(), files.size(), fill, nullptr, 0));
}

static drive_import_result_t take(int expect = DRIVE_OK) {
  drive_import_result_t r;
  TEST_ASSERT_EQUAL(expect, drive_import(&ops, img, IMG_SIZE, &manifest, &r));
  return r;
}

static bool no_journal_left(void) {
  for (auto &f : fs.files) {
    if (f.first.compare(0, 13, ".drive/stage/") == 0 || f.first == DRIVE_PLAN || f.first == DRIVE_PLAN_TMP) {
      return false;
    }
  }
  return !fs.dirs.count(DRIVE_STAGE_DIR);
}

// ---- tests ----

void test_shows_only_the_layout(void) {
  writer_folder();
  drive_export_result_t r = show();
  TEST_ASSERT_EQUAL(5, r.shown);
  computer_mounts();
  TEST_ASSERT_EQUAL(5, (int)computer.size());
  TEST_ASSERT_EQUAL_STRING("hello from the X\n", computer["documents/Notes/Hello.md"].c_str());
  TEST_ASSERT_FALSE(computer.count("apps-private.bin"));
  TEST_ASSERT_FALSE(computer.count("documents/Notes/draft.doc"));
}

void test_nothing_changed_writes_nothing(void) {
  writer_folder();
  show();
  computer_mounts();
  computer_ejects();
  int before = fs.ops;
  drive_import_result_t r = take();
  TEST_ASSERT_EQUAL(5, r.unchanged);
  TEST_ASSERT_EQUAL(0, r.added + r.updated + r.conflicts);
  TEST_ASSERT_EQUAL(before, fs.ops);
}

void test_takes_edits_and_new_files(void) {
  writer_folder();
  show();
  computer_mounts();
  computer["documents/Notes/Hello.md"] = "edited on the laptop\n";
  computer["documents/Trip/Day 1.md"] = "new\n";
  computer["settings/fonts/Mono.ttf"] = std::string(100, 'm');
  computer_ejects();
  drive_import_result_t r = take();
  TEST_ASSERT_EQUAL(1, r.updated);
  TEST_ASSERT_EQUAL(2, r.added);
  TEST_ASSERT_EQUAL(0, r.conflicts);
  TEST_ASSERT_EQUAL_STRING("edited on the laptop\n", fs.files["documents/Notes/Hello.md"].c_str());
  TEST_ASSERT_EQUAL_STRING("new\n", fs.files["documents/Trip/Day 1.md"].c_str());
  TEST_ASSERT_EQUAL(100, (int)fs.files["settings/fonts/Mono.ttf"].size());
  TEST_ASSERT_TRUE(no_journal_left());
}

void test_changed_on_both_sides_keeps_both(void) {
  writer_folder();
  show();
  computer_mounts();
  computer["documents/Notes/Hello.md"] = "laptop version\n";
  computer_ejects();
  fs.files["documents/Notes/Hello.md"] = "X version\n"; // written on the X meanwhile
  drive_import_result_t r = take();
  TEST_ASSERT_EQUAL(1, r.conflicts);
  TEST_ASSERT_EQUAL_STRING("X version\n", fs.files["documents/Notes/Hello.md"].c_str());
  TEST_ASSERT_EQUAL_STRING("laptop version\n", fs.files["documents/Notes/Hello (computer).md"].c_str());
  TEST_ASSERT_EQUAL_STRING("documents/Notes/Hello.md\tdocuments/Notes/Hello (computer).md\n",
                           fs.files[DRIVE_CONFLICTS].c_str());
  // a second round adds a second copy and a second line, never replaces the first
  show();
  computer_mounts();
  computer["documents/Notes/Hello.md"] = "laptop again\n";
  computer_ejects();
  fs.files["documents/Notes/Hello.md"] = "X again\n";
  take();
  TEST_ASSERT_EQUAL_STRING("laptop version\n", fs.files["documents/Notes/Hello (computer).md"].c_str());
  TEST_ASSERT_EQUAL_STRING("laptop again\n", fs.files["documents/Notes/Hello (computer 2).md"].c_str());
  TEST_ASSERT_EQUAL(2, (int)std::count(fs.files[DRIVE_CONFLICTS].begin(), fs.files[DRIVE_CONFLICTS].end(), '\n'));
}

void test_a_new_file_meeting_one_on_the_x_keeps_both(void) {
  writer_folder();
  show();
  computer_mounts();
  computer["documents/Notes/Later.md"] = "from the laptop\n";
  computer_ejects();
  fs.files["documents/Notes/Later.md"] = "from the X\n";
  drive_import_result_t r = take();
  TEST_ASSERT_EQUAL(1, r.conflicts);
  TEST_ASSERT_EQUAL_STRING("from the X\n", fs.files["documents/Notes/Later.md"].c_str());
  TEST_ASSERT_EQUAL_STRING("from the laptop\n", fs.files["documents/Notes/Later (computer).md"].c_str());
}

void test_x_changed_computer_did_not_keeps_the_x(void) {
  writer_folder();
  show();
  computer_mounts();
  computer_ejects();
  fs.files["documents/Notes/Hello.md"] = "newer on the X\n";
  drive_import_result_t r = take();
  TEST_ASSERT_EQUAL(0, r.updated + r.conflicts);
  TEST_ASSERT_EQUAL_STRING("newer on the X\n", fs.files["documents/Notes/Hello.md"].c_str());
}

void test_deletes_and_foreign_files_are_left_alone(void) {
  writer_folder();
  show();
  computer_mounts();
  computer.erase("documents/Notes/Shopping.txt");
  computer["documents/Notes/._Hello.md"] = "macOS resource fork";
  computer["notes.docx"] = "elsewhere on the drive";
  computer["documents/Notes/What a long title that does not fit the X at all, really.md"] = "too long";
  computer["documents/Notes/image.png"] = "not a writer type";
  computer_ejects();
  drive_import_result_t r = take();
  TEST_ASSERT_EQUAL(1, r.kept_deleted);
  TEST_ASSERT_EQUAL(1, r.left_alone);
  TEST_ASSERT_EQUAL(2, r.bad_names);
  TEST_ASSERT_EQUAL(0, r.added);
  TEST_ASSERT_TRUE(fs.files.count("documents/Notes/Shopping.txt"));
  TEST_ASSERT_FALSE(fs.files.count("notes.docx"));
  TEST_ASSERT_FALSE(fs.files.count("documents/Notes/._Hello.md"));
  TEST_ASSERT_EQUAL_STRING("not the writer's layout", fs.files["apps-private.bin"].c_str());
}

void test_names_differing_only_in_case_show_once(void) {
  writer_folder();
  put("documents/Notes/hello.md", "lower case twin");
  drive_export_result_t r = show();
  TEST_ASSERT_EQUAL(1, r.not_shown);
  // the twin not shown is never replaced by what the computer writes under that name
  computer_mounts();
  computer_ejects();
  take();
  TEST_ASSERT_EQUAL_STRING("lower case twin", fs.files["documents/Notes/hello.md"].c_str());
}

void test_a_full_x_takes_nothing(void) {
  writer_folder();
  show();
  computer_mounts();
  computer["documents/Notes/Hello.md"] = "edited\n";
  computer["documents/Notes/Big.md"] = std::string(200000, 'b');
  computer_ejects();
  fs.capacity = fs.used() + 100000;
  std::map<std::string, std::string> before = fs.files;
  take(DRIVE_EFULL);
  TEST_ASSERT_TRUE(before == fs.files);
  TEST_ASSERT_TRUE(no_journal_left());
}

void test_running_full_while_staging_takes_nothing(void) {
  // free_bytes said yes, the writes say no (LittleFS's count is an estimate)
  writer_folder();
  show();
  computer_mounts();
  computer["documents/Notes/Hello.md"] = "edited\n";
  computer["documents/Notes/Big.md"] = std::string(60000, 'b');
  computer_ejects();
  std::map<std::string, std::string> before = fs.files;
  int64_t real = fs.used() + 30000;
  auto lie = [](void *) -> int64_t { return 1 << 30; };
  drive_fs_t lying = ops;
  lying.free_bytes = lie;
  fs.capacity = real;
  drive_import_result_t r;
  TEST_ASSERT_EQUAL(DRIVE_EFULL, drive_import(&lying, img, IMG_SIZE, &manifest, &r));
  TEST_ASSERT_TRUE(before == fs.files);
  TEST_ASSERT_TRUE(no_journal_left());
}

void test_a_damaged_image_takes_nothing(void) {
  writer_folder();
  show();
  computer_mounts();
  computer["documents/Notes/Hello.md"] = "edited\n";
  computer_ejects();
  img[510] = 0; // e.g. reformatted
  std::map<std::string, std::string> before = fs.files;
  take(DRIVE_EIMAGE);
  TEST_ASSERT_TRUE(before == fs.files);
}

// Power lost at every changing call of an import in turn: after the next start's recovery the
// files are all as before or all as after, never a mix, and no journal is left
void test_power_loss_at_any_point_is_all_or_nothing(void) {
  writer_folder();
  show();
  computer_mounts();
  computer["documents/Notes/Hello.md"] = "edited\n";
  computer["documents/Trip/Day 1.md"] = std::string(10000, 'd');
  computer["settings/settings.json"] = "{\"font\":\"Mono\"}";
  computer_ejects();
  fs.files["settings/settings.json"] = "{\"font\":\"X changed it\"}"; // a conflict, for the list
  FakeFs start = fs;
  take();
  std::map<std::string, std::string> after = fs.files;
  int total = fs.ops - start.ops;
  TEST_ASSERT_TRUE(total > 10);
  auto layout = [](const std::map<std::string, std::string> &m) {
    std::map<std::string, std::string> out;
    for (auto &f : m) {
      if (f.first.compare(0, 7, ".drive/") != 0 || f.first == DRIVE_CONFLICTS) out.insert(f);
    }
    return out;
  };
  int old_state = 0, new_state = 0;
  for (int k = 0; k < total; k++) {
    fs = start;
    fs.cut_at = fs.ops + k;
    drive_import_result_t r;
    int rc = drive_import(&ops, img, IMG_SIZE, &manifest, &r);
    if (rc == DRIVE_OK) TEST_ASSERT_TRUE(layout(fs.files) == layout(after)); // cut in the cleanup after it
    fs.cut = false; // the next start
    fs.cut_at = -1;
    TEST_ASSERT_EQUAL_MESSAGE(DRIVE_OK, drive_recover(&ops), std::to_string(k).c_str());
    TEST_ASSERT_TRUE_MESSAGE(no_journal_left(), std::to_string(k).c_str());
    auto now = layout(fs.files);
    bool is_old = now == layout(start.files);
    bool is_new = now == layout(after);
    TEST_ASSERT_TRUE_MESSAGE(is_old || is_new, std::to_string(k).c_str());
    old_state += is_old;
    new_state += is_new;
  }
  TEST_ASSERT_TRUE(old_state > 0 && new_state > 0);
}

// A cut while the recovery itself runs: it is run again at the next start
void test_power_loss_during_recovery(void) {
  writer_folder();
  show();
  computer_mounts();
  computer["documents/Notes/Hello.md"] = "edited\n";
  computer["documents/Notes/Shopping.txt"] = "eggs\n";
  computer_ejects();
  FakeFs start = fs;
  take();
  std::map<std::string, std::string> after = fs.files;
  int total = fs.ops - start.ops;
  for (int k = 0; k < total; k++) {
    fs = start;
    fs.cut_at = fs.ops + k;
    drive_import_result_t r;
    drive_import(&ops, img, IMG_SIZE, &manifest, &r);
    if (fs.files.count(DRIVE_PLAN) == 0) continue; // cut before the commit
    for (int j = 0; j < 4; j++) {
      fs.cut = false;
      fs.cut_at = fs.ops + j;
      drive_recover(&ops);
    }
    fs.cut = false;
    fs.cut_at = -1;
    TEST_ASSERT_EQUAL(DRIVE_OK, drive_recover(&ops));
    TEST_ASSERT_EQUAL_STRING("edited\n", fs.files["documents/Notes/Hello.md"].c_str());
    TEST_ASSERT_EQUAL_STRING("eggs\n", fs.files["documents/Notes/Shopping.txt"].c_str());
    TEST_ASSERT_TRUE(no_journal_left());
  }
}

void test_a_damaged_plan_applies_nothing(void) {
  writer_folder();
  put(".drive/plan", ".drive/stage/0\t../../etc/passwd\nend\n");
  put(".drive/stage/0", "evil");
  std::map<std::string, std::string> before = fs.files;
  TEST_ASSERT_TRUE(drive_recover(&ops) < 0);
  TEST_ASSERT_TRUE(before == fs.files);
  fs.files[".drive/plan"] = ".drive/stage/0\tdocuments/Notes/Hello.md\n"; // no "end"
  TEST_ASSERT_TRUE(drive_recover(&ops) < 0);
  TEST_ASSERT_EQUAL_STRING("hello from the X\n", fs.files["documents/Notes/Hello.md"].c_str());
}

void test_a_leftover_stage_without_plan_is_dropped(void) {
  writer_folder();
  put(".drive/stage/0", "half");
  put(".drive/plan.tmp", ".drive/stage/0\tdocuments/Notes/Hello.md\n");
  TEST_ASSERT_EQUAL(DRIVE_OK, drive_recover(&ops));
  TEST_ASSERT_TRUE(no_journal_left());
  TEST_ASSERT_EQUAL_STRING("hello from the X\n", fs.files["documents/Notes/Hello.md"].c_str());
}

void test_too_many_files(void) {
  writer_folder();
  for (int i = 0; i < DRIVE_MAX_FILES; i++) put("documents/Many/" + std::to_string(i) + ".md", "x");
  drive_export_result_t r;
  TEST_ASSERT_EQUAL(DRIVE_ETOOMANY, drive_export(&ops, img, IMG_SIZE, &manifest, 0, &r));
}

void setUp(void) {
  if (!img) img = (uint8_t *)malloc(IMG_SIZE);
}
void tearDown(void) {}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_shows_only_the_layout);
  RUN_TEST(test_nothing_changed_writes_nothing);
  RUN_TEST(test_takes_edits_and_new_files);
  RUN_TEST(test_changed_on_both_sides_keeps_both);
  RUN_TEST(test_a_new_file_meeting_one_on_the_x_keeps_both);
  RUN_TEST(test_x_changed_computer_did_not_keeps_the_x);
  RUN_TEST(test_deletes_and_foreign_files_are_left_alone);
  RUN_TEST(test_names_differing_only_in_case_show_once);
  RUN_TEST(test_a_full_x_takes_nothing);
  RUN_TEST(test_running_full_while_staging_takes_nothing);
  RUN_TEST(test_a_damaged_image_takes_nothing);
  RUN_TEST(test_power_loss_at_any_point_is_all_or_nothing);
  RUN_TEST(test_power_loss_during_recovery);
  RUN_TEST(test_a_damaged_plan_applies_nothing);
  RUN_TEST(test_a_leftover_stage_without_plan_is_dropped);
  RUN_TEST(test_too_many_files);
  return UNITY_END();
}
