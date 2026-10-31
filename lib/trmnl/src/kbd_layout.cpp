// Keyboard layouts (kbd_layout.h). One shape for every layout: rows {usage, plain, Shift, AltGr} for the keys that
// differ from the US base below; a letter key not listed types its US letter. The Belgian rows are trmnl-writer's
// kbmap.c table (MIT, Copyright (c) 2026 ExcuseMi), unchanged but for the shape.
#include "kbd_layout.h"

#include <string.h>

typedef struct {
  uint8_t usage;
  uint16_t n, s, a; // the character plain, with Shift, with AltGr; 0: none
} kbd_row_t;

// US QWERTY: the keys that are not letters
static const kbd_row_t US[] = {
    {0x1E, '1', '!', 0},  {0x1F, '2', '@', 0},  {0x20, '3', '#', 0},   {0x21, '4', '$', 0},  {0x22, '5', '%', 0},
    {0x23, '6', '^', 0},  {0x24, '7', '&', 0},  {0x25, '8', '*', 0},   {0x26, '9', '(', 0},  {0x27, '0', ')', 0},
    {0x2C, ' ', ' ', ' '}, {0x2D, '-', '_', 0}, {0x2E, '=', '+', 0},   {0x2F, '[', '{', 0},  {0x30, ']', '}', 0},
    {0x31, '\\', '|', 0}, {0x32, '\\', '|', 0}, {0x33, ';', ':', 0},   {0x34, '\'', '"', 0}, {0x35, '`', '~', 0},
    {0x36, ',', '<', 0},  {0x37, '.', '>', 0},  {0x38, '/', '?', 0},   {0x64, '\\', '|', 0},
};

// Belgian AZERTY (trmnl-writer kbmap.c BE, plus the letters it swaps and AltGr+E)
static const kbd_row_t BE[] = {
    {0x1E, '&', '1', '|'},   {0x1F, 0xE9, '2', '@'},  {0x20, '"', '3', '#'},   {0x21, '\'', '4', 0},
    {0x22, '(', '5', 0},     {0x23, 0xA7, '6', '^'},  {0x24, 0xE8, '7', 0},    {0x25, '!', '8', 0},
    {0x26, 0xE7, '9', '{'},  {0x27, 0xE0, '0', '}'},  {0x2D, ')', 0xB0, 0},    {0x2E, '-', '_', 0},
    {0x2F, '^', 0xA8, '['},  {0x30, '$', '*', ']'},   {0x31, 0xB5, 0xA3, '`'}, {0x32, 0xB5, 0xA3, '`'},
    {0x33, 'm', 'M', 0},     {0x34, 0xF9, '%', 0xB4}, {0x35, 0xB2, 0xB3, 0},   {0x36, ';', '.', 0},
    {0x37, ':', '/', 0},     {0x38, '=', '+', '~'},   {0x64, '<', '>', '\\'},  {0x10, ',', '?', 0},
    {0x14, 'a', 'A', 0},     {0x04, 'q', 'Q', 0},     {0x1A, 'z', 'Z', 0},     {0x1D, 'w', 'W', 0},
    {0x08, 'e', 'E', 0x20AC},
};

// French AZERTY
static const kbd_row_t FR[] = {
    {0x1E, '&', '1', 0},     {0x1F, 0xE9, '2', '~'},  {0x20, '"', '3', '#'},     {0x21, '\'', '4', '{'},
    {0x22, '(', '5', '['},   {0x23, '-', '6', '|'},   {0x24, 0xE8, '7', '`'},    {0x25, '_', '8', '\\'},
    {0x26, 0xE7, '9', '^'},  {0x27, 0xE0, '0', '@'},  {0x2D, ')', 0xB0, ']'},    {0x2E, '=', '+', '}'},
    {0x2F, '^', 0xA8, 0},    {0x30, '$', 0xA3, 0xA4}, {0x31, '*', 0xB5, 0},      {0x32, '*', 0xB5, 0},
    {0x33, 'm', 'M', 0},     {0x34, 0xF9, '%', 0},    {0x35, 0xB2, 0, 0},        {0x36, ';', '.', 0},
    {0x37, ':', '/', 0},     {0x38, '!', 0xA7, 0},    {0x64, '<', '>', 0},       {0x10, ',', '?', 0},
    {0x14, 'a', 'A', 0},     {0x04, 'q', 'Q', 0},     {0x1A, 'z', 'Z', 0},       {0x1D, 'w', 'W', 0},
    {0x08, 'e', 'E', 0x20AC},
};

// UK QWERTY: the keys that differ from US
static const kbd_row_t UK[] = {
    {0x1F, '2', '"', 0},    {0x20, '3', 0xA3, 0}, {0x21, '4', '$', 0x20AC}, {0x34, '\'', '@', 0},
    {0x31, '#', '~', 0},    {0x32, '#', '~', 0},  {0x35, '`', 0xAC, 0xA6},  {0x64, '\\', '|', 0},
};

// German QWERTZ
static const kbd_row_t DE[] = {
    {0x1E, '1', '!', 0},     {0x1F, '2', '"', 0xB2},  {0x20, '3', 0xA7, 0xB3}, {0x21, '4', '$', 0},
    {0x22, '5', '%', 0},     {0x23, '6', '&', 0},     {0x24, '7', '/', '{'},   {0x25, '8', '(', '['},
    {0x26, '9', ')', ']'},   {0x27, '0', '=', '}'},   {0x2D, 0xDF, '?', '\\'}, {0x2E, 0xB4, '`', 0},
    {0x2F, 0xFC, 0xDC, 0},   {0x30, '+', '*', '~'},   {0x31, '#', '\'', 0},    {0x32, '#', '\'', 0},
    {0x33, 0xF6, 0xD6, 0},   {0x34, 0xE4, 0xC4, 0},   {0x35, '^', 0xB0, 0},    {0x36, ',', ';', 0},
    {0x37, '.', ':', 0},     {0x38, '-', '_', 0},     {0x64, '<', '>', '|'},   {0x1C, 'z', 'Z', 0},
    {0x1D, 'y', 'Y', 0},     {0x14, 'q', 'Q', '@'},   {0x08, 'e', 'E', 0x20AC}, {0x10, 'm', 'M', 0xB5},
};

typedef struct {
  const char *code, *name;
  const kbd_row_t *rows;
  size_t n;
} kbd_def_t;

#define ROWS(t) t, sizeof(t) / sizeof(t[0])
static const kbd_def_t LAYOUTS[KBD_LAYOUT_COUNT] = {
    {"be", "Belgian AZERTY", ROWS(BE)}, {"fr", "French AZERTY", ROWS(FR)}, {"us", "US QWERTY", ROWS(US)},
    {"uk", "UK QWERTY", ROWS(UK)},      {"de", "German QWERTZ", ROWS(DE)},
};

static const kbd_row_t *find(const kbd_row_t *rows, size_t n, uint8_t usage) {
  for (size_t i = 0; i < n; i++) {
    if (rows[i].usage == usage) return &rows[i];
  }
  return nullptr;
}

const char *kbd_layout_name(int layout) {
  return LAYOUTS[layout >= 0 && layout < KBD_LAYOUT_COUNT ? layout : KBD_LAYOUT_DEFAULT].name;
}

const char *kbd_layout_code(int layout) {
  return LAYOUTS[layout >= 0 && layout < KBD_LAYOUT_COUNT ? layout : KBD_LAYOUT_DEFAULT].code;
}

int kbd_layout_from_code(const char *code) {
  if (!code) return -1;
  for (int i = 0; i < KBD_LAYOUT_COUNT; i++) {
    if (!strcmp(code, LAYOUTS[i].code)) return i;
  }
  return -1;
}

uint32_t kbd_layout_char(int layout, uint8_t usage, uint8_t mods) {
  if (layout < 0 || layout >= KBD_LAYOUT_COUNT) return 0;
  const bool shift = (mods & APP_MOD_SHIFT) != 0, altgr = (mods & APP_MOD_ALTGR) != 0;
  if (usage >= 0x54 && usage <= 0x63 && usage != 0x58) { // the keypad, Num Lock on: the same on every layout
    static const char KP[] = "/*-+\0" "1234567890.";
    return (uint8_t)KP[usage - 0x54];
  }
  const kbd_def_t &d = LAYOUTS[layout];
  const kbd_row_t *r = find(d.rows, d.n, usage);
  if (!r) r = find(US, sizeof(US) / sizeof(US[0]), usage);
  if (r) return altgr ? (shift ? 0 : r->a) : shift ? r->s : r->n;
  if (usage >= 0x04 && usage <= 0x1D) { // a letter that types its US letter
    if (altgr) return 0;
    return (uint32_t)((shift ? 'A' : 'a') + (usage - 0x04));
  }
  return 0;
}

int kbd_utf8(uint32_t cp, char out[5]) {
  memset(out, 0, 5);
  if (cp == 0 || cp > 0xFFFF) return 0;
  if (cp < 0x80) {
    out[0] = (char)cp;
    return 1;
  }
  if (cp < 0x800) {
    out[0] = (char)(0xC0 | (cp >> 6));
    out[1] = (char)(0x80 | (cp & 0x3F));
    return 2;
  }
  out[0] = (char)(0xE0 | (cp >> 12));
  out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
  out[2] = (char)(0x80 | (cp & 0x3F));
  return 3;
}

bool kbd_layout_apply_for_app(bool app_wants_chars, int layout, app_event_t *ev) {
  return app_wants_chars && kbd_layout_apply(layout, ev);
}

bool kbd_layout_apply(int layout, app_event_t *ev) {
  if (!ev || (ev->type != APP_EVENT_KEY_DOWN && ev->type != APP_EVENT_KEY_UP) || ev->key != APP_KEY_HID) return false;
  uint32_t cp = kbd_layout_char(layout, ev->hid_usage, ev->modifiers);
  char u[5];
  if (!kbd_utf8(cp, u)) return false;
  ev->key = APP_KEY_CHAR;
  memcpy(ev->utf8, u, sizeof(ev->utf8));
  return true;
}
