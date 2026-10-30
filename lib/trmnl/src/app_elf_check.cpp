#include "app_elf_check.h"

#include <string.h>

// The ELF structures, as the loader reads them (elf_loader include/private/elf_types.h). Read with
// memcpy: the checker makes no assumption about the buffer's alignment.
typedef struct {
  uint8_t ident[16];
  uint16_t type;
  uint16_t machine;
  uint32_t version;
  uint32_t entry;
  uint32_t phoff;
  uint32_t shoff;
  uint32_t flags;
  uint16_t ehsize;
  uint16_t phentsize;
  uint16_t phnum;
  uint16_t shentsize;
  uint16_t shnum;
  uint16_t shstrndx;
} elf_ehdr_t;

typedef struct {
  uint32_t name;
  uint32_t type;
  uint32_t flags;
  uint32_t addr;
  uint32_t offset;
  uint32_t size;
  uint32_t link;
  uint32_t info;
  uint32_t addralign;
  uint32_t entsize;
} elf_shdr_t;

typedef struct {
  uint32_t name;
  uint32_t value;
  uint32_t size;
  uint8_t info;
  uint8_t other;
  uint16_t shndx;
} elf_sym_t;

typedef struct {
  uint32_t offset;
  uint32_t info;
  int32_t addend;
} elf_rela_t;

#define ET_EXEC 2
#define ET_DYN 3
#define EM_XTENSA 94
#define SHT_PROGBITS 1
#define SHT_SYMTAB 2
#define SHT_STRTAB 3
#define SHT_RELA 4
#define SHT_NOBITS 8
#define SHT_DYNSYM 11
#define SHF_WRITE 1
#define SHF_ALLOC 2
#define SHF_EXECINSTR 4

// The relocation types the loader handles (elf_loader src/arch/esp_elf_xtensa.c); it ignores the
// error it logs for any other, leaving the code unrelocated
#define R_XTENSA_NONE 0
#define R_XTENSA_RTLD 2
#define R_XTENSA_GLOB_DAT 3
#define R_XTENSA_JMP_SLOT 4
#define R_XTENSA_RELATIVE 5

// The sections the loader copies, in its order (ELF_SEC_TEXT .. ELF_SEC_DRLRO)
enum { SEC_TEXT, SEC_BSS, SEC_DATA, SEC_RODATA, SEC_DRLRO, SEC_COUNT };

typedef struct {
  uint32_t vaddr;
  uint32_t size;
  uint32_t align;
  uint32_t file_off;
  uint32_t load_off; // where the loader puts it: offset in its text or data buffer
  bool found;
} loaded_sec_t;

// [off, off + size) lies within len bytes
static bool in_range(uint32_t off, uint32_t size, size_t len) { return off <= len && size <= len - off; }

// A NUL-terminated string at `off` in a string table section
static bool string_ok(const uint8_t *buf, const elf_shdr_t &tab, uint32_t off) {
  if (off >= tab.size) return false;
  return memchr(buf + tab.offset + off, '\0', tab.size - off) != nullptr;
}

static const char *string_at(const uint8_t *buf, const elf_shdr_t &tab, uint32_t off) {
  return (const char *)buf + tab.offset + off;
}

static elf_shdr_t section(const uint8_t *buf, const elf_ehdr_t &eh, uint32_t i) {
  elf_shdr_t sh;
  memcpy(&sh, buf + eh.shoff + i * sizeof(elf_shdr_t), sizeof(sh));
  return sh;
}

// The loaded address of an ELF address, as esp_elf_map_sym() finds it: the first loaded section
// that holds it. Sets *sec, or returns false if none does.
static bool map_addr(const loaded_sec_t *secs, uint32_t addr, int *sec) {
  for (int i = 0; i < SEC_COUNT; i++) {
    if (secs[i].found && secs[i].size && addr >= secs[i].vaddr && addr - secs[i].vaddr < secs[i].size) {
      *sec = i;
      return true;
    }
  }
  return false;
}

// The relocations, as esp_elf_relocate() applies them: each one's symbol and its name must be in
// their tables, and the word it patches in a loaded section and 4-byte aligned there
static bool relocations_ok(const uint8_t *buf, const elf_ehdr_t &eh, const elf_shdr_t &rela,
                           const loaded_sec_t *secs) {
  if (rela.link >= eh.shnum) return false;
  elf_shdr_t symtab = section(buf, eh, rela.link);
  if (symtab.type != SHT_SYMTAB && symtab.type != SHT_DYNSYM) return false;
  if (symtab.link >= eh.shnum) return false;
  elf_shdr_t strtab = section(buf, eh, symtab.link);
  if (strtab.type != SHT_STRTAB) return false;
  uint32_t nsyms = symtab.size / sizeof(elf_sym_t);
  uint32_t nrela = rela.size / sizeof(elf_rela_t);
  for (uint32_t i = 0; i < nrela; i++) {
    elf_rela_t r;
    memcpy(&r, buf + rela.offset + i * sizeof(elf_rela_t), sizeof(r));
    uint32_t type = r.info & 0xff;
    uint32_t sym_index = r.info >> 8;
    if (sym_index >= nsyms) return false;
    elf_sym_t sym;
    memcpy(&sym, buf + symtab.offset + sym_index * sizeof(elf_sym_t), sizeof(sym));
    if (!string_ok(buf, strtab, sym.name)) return false;
    if (type == R_XTENSA_NONE || type == R_XTENSA_RTLD) continue;
    if (type != R_XTENSA_GLOB_DAT && type != R_XTENSA_JMP_SLOT && type != R_XTENSA_RELATIVE) return false;
    int sec;
    if (!map_addr(secs, r.offset, &sec)) return false;
    uint32_t at = r.offset - secs[sec].vaddr;
    if (secs[sec].size < 4 || at > secs[sec].size - 4) return false;
    if ((secs[sec].load_off + at) % 4 != 0) return false;
  }
  return true;
}

static app_elf_result_t check_info(const uint8_t *buf, const elf_shdr_t &sh, size_t free_mem, uint32_t load_size,
                                   app_info_t *info) {
  if (sh.type != SHT_PROGBITS || sh.size < sizeof(app_info_t)) return APP_ELF_BAD_INFO;
  memcpy(info, buf + sh.offset, sizeof(*info));
  if (info->magic != APP_INFO_MAGIC || info->api_version == 0) return APP_ELF_BAD_INFO;
  if (memchr(info->name, '\0', sizeof(info->name)) == nullptr || info->name[0] == '\0') return APP_ELF_BAD_INFO;
  if (info->api_version > APP_API_VERSION) return APP_ELF_API_TOO_NEW;
  if (info->stack_size == 0) info->stack_size = APP_DEFAULT_STACK;
  if (info->stack_size > APP_MAX_STACK) return APP_ELF_STACK_TOO_BIG;
  if (load_size > free_mem) return APP_ELF_TOO_BIG;
  return APP_ELF_OK;
}

app_elf_result_t app_elf_check(const uint8_t *buf, size_t len, size_t free_mem, app_info_t *info) {
  if (!buf || !info) return APP_ELF_NOT_ELF;
  memset(info, 0, sizeof(*info));
  if (len < 4 || memcmp(buf, "\x7f" "ELF", 4) != 0) return len < 4 ? APP_ELF_TRUNCATED : APP_ELF_NOT_ELF;
  if (len < sizeof(elf_ehdr_t)) return APP_ELF_TRUNCATED;
  elf_ehdr_t eh;
  memcpy(&eh, buf, sizeof(eh));
  if (eh.ident[4] != 1 || eh.ident[5] != 1 || eh.machine != EM_XTENSA) return APP_ELF_WRONG_ARCH;
  if (eh.type != ET_DYN && eh.type != ET_EXEC) return APP_ELF_WRONG_ARCH;
  if (eh.shentsize != sizeof(elf_shdr_t) || eh.shnum == 0 || eh.shstrndx >= eh.shnum) return APP_ELF_BAD_LAYOUT;
  if (!in_range(eh.shoff, (uint32_t)eh.shnum * sizeof(elf_shdr_t), len)) return APP_ELF_TRUNCATED;

  // Every section's contents in the file, the name table first
  for (uint32_t i = 0; i < eh.shnum; i++) {
    elf_shdr_t sh = section(buf, eh, i);
    if (sh.type == SHT_NOBITS) continue;
    if (!in_range(sh.offset, sh.size, len)) return APP_ELF_TRUNCATED;
  }
  elf_shdr_t names = section(buf, eh, eh.shstrndx);
  if (names.type != SHT_STRTAB) return APP_ELF_BAD_LAYOUT;

  // The sections the loader copies, found as esp_elf_load_section() finds them (the last of a name)
  static const char *const sec_names[SEC_COUNT] = {".text", ".bss", ".data", ".rodata", ".data.rel.ro"};
  loaded_sec_t secs[SEC_COUNT] = {};
  int info_index = -1;
  for (uint32_t i = 0; i < eh.shnum; i++) {
    elf_shdr_t sh = section(buf, eh, i);
    if (!string_ok(buf, names, sh.name)) return APP_ELF_BAD_LAYOUT;
    const char *name = string_at(buf, names, sh.name);
    if (strcmp(name, APP_INFO_SECTION) == 0) info_index = (int)i;
    for (int s = 0; s < SEC_COUNT; s++) {
      if (strcmp(name, sec_names[s]) != 0 || !(sh.flags & SHF_ALLOC)) continue;
      bool type_ok = s == SEC_BSS ? sh.type == SHT_NOBITS && (sh.flags & SHF_WRITE) : sh.type == SHT_PROGBITS;
      if (s == SEC_TEXT) type_ok = type_ok && (sh.flags & SHF_EXECINSTR);
      if (s == SEC_DATA) type_ok = type_ok && (sh.flags & SHF_WRITE);
      if (!type_ok) continue;
      secs[s] = {sh.addr, sh.size, sh.addralign > 1 ? sh.addralign : 1, sh.offset, 0, true};
    }
  }

  // .text, copied rounded up to 4 bytes, holding the entry
  loaded_sec_t &text = secs[SEC_TEXT];
  if (!text.found || text.size == 0) return APP_ELF_BAD_LAYOUT;
  if (text.size > UINT32_MAX - 3) return APP_ELF_BAD_LAYOUT;
  text.size = (text.size + 3) & ~3u;
  if (!in_range(text.file_off, text.size, len)) return APP_ELF_TRUNCATED;
  if (eh.entry < text.vaddr || eh.entry - text.vaddr >= text.size) return APP_ELF_BAD_LAYOUT;
  // The loader copies .text rounded up to 4 bytes, and maps an address to the first section that
  // holds it, .text first: a section starting in that rounding would be read through the
  // instruction bus (apps/build.sh aligns .rodata to prevent it)
  for (int s = SEC_BSS; s < SEC_COUNT; s++) {
    if (!secs[s].found || !secs[s].size) continue;
    uint64_t start = secs[s].vaddr, end = start + secs[s].size;
    if (start < (uint64_t)text.vaddr + text.size && end > text.vaddr) return APP_ELF_BAD_LAYOUT;
  }

  // The data sections, laid out in one buffer as the loader does: .data, .rodata, .data.rel.ro, .bss
  static const int data_order[] = {SEC_DATA, SEC_RODATA, SEC_DRLRO, SEC_BSS};
  uint64_t data_off = 0;
  for (int s : data_order) {
    if (!secs[s].found || !secs[s].size) continue;
    uint32_t align = secs[s].align;
    if (align & (align - 1)) return APP_ELF_BAD_LAYOUT;
    data_off += (uint32_t)(-(uint32_t)data_off) & (align - 1);
    secs[s].load_off = (uint32_t)data_off;
    data_off += secs[s].size;
    if (data_off > UINT32_MAX / 2) return APP_ELF_TOO_BIG;
  }

  for (uint32_t i = 0; i < eh.shnum; i++) {
    elf_shdr_t sh = section(buf, eh, i);
    if (sh.type == SHT_RELA && !relocations_ok(buf, eh, sh, secs)) return APP_ELF_BAD_LAYOUT;
  }

  if (info_index < 0) return APP_ELF_NO_INFO;
  // What the loader allocates: .text, the data buffer with its padding, and some slack per block
  uint32_t load_size = text.size + (uint32_t)data_off + 2 * 64;
  return check_info(buf, section(buf, eh, (uint32_t)info_index), free_mem, load_size, info);
}

const char *app_elf_result_str(app_elf_result_t result) {
  switch (result) {
  case APP_ELF_OK:
    return "ok";
  case APP_ELF_TRUNCATED:
    return "file is truncated";
  case APP_ELF_NOT_ELF:
    return "not an app (no ELF header)";
  case APP_ELF_WRONG_ARCH:
    return "built for another chip";
  case APP_ELF_BAD_LAYOUT:
    return "file is damaged";
  case APP_ELF_NO_INFO:
    return "not a TRMNL app (no app_info)";
  case APP_ELF_BAD_INFO:
    return "bad app_info";
  case APP_ELF_API_TOO_NEW:
    return "needs a newer firmware";
  case APP_ELF_STACK_TOO_BIG:
    return "asks for too big a stack";
  case APP_ELF_TOO_BIG:
    return "too big for free memory";
  }
  return "unknown error";
}
