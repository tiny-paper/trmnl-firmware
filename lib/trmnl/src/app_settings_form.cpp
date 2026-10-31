// An app's settings form (app_settings_form.h): declaration and value checks, no I/O.
#include "app_settings_form.h"

#include <stdio.h>
#include <string.h>

static bool key_ok(const char *k) {
  size_t n = k ? strnlen(k, ASF_KEY_MAX + 1) : 0;
  if (n == 0 || n > ASF_KEY_MAX) return false;
  for (size_t i = 0; i < n; i++) {
    char c = k[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return false;
  }
  return true;
}

// UTF-8 without control characters (C0, DEL); false for a broken sequence
static bool text_ok(const char *s, size_t len) {
  for (size_t i = 0; i < len;) {
    uint8_t b = (uint8_t)s[i];
    if (b < 0x20 || b == 0x7F) return false;
    size_t n = b < 0x80 ? 1 : (b & 0xE0) == 0xC0 ? 2 : (b & 0xF0) == 0xE0 ? 3 : (b & 0xF8) == 0xF0 ? 4 : 0;
    if (n == 0 || (n == 2 && b < 0xC2) || i + n > len) return false;
    for (size_t j = 1; j < n; j++) {
      if (((uint8_t)s[i + j] & 0xC0) != 0x80) return false;
    }
    i += n;
  }
  return true;
}

// Counts the choices of "a|b|c"; false for an empty or repeated one or too many
static bool choices_ok(const char *c, uint8_t *count) {
  size_t len = c ? strnlen(c, ASF_CHOICES_BYTES + 1) : 0;
  if (len == 0 || len > ASF_CHOICES_BYTES || !text_ok(c, len)) return false;
  int n = 0;
  for (size_t at = 0; at <= len;) {
    const char *bar = (const char *)memchr(c + at, '|', len - at);
    size_t end = bar ? (size_t)(bar - c) : len;
    if (end == at || ++n > ASF_CHOICES_MAX) return false;
    for (size_t prev = 0; prev < at;) { // the same text earlier
      const char *pb = (const char *)memchr(c + prev, '|', at - prev);
      size_t pend = (size_t)(pb - c);
      if (pend - prev == end - at && !memcmp(c + prev, c + at, end - at)) return false;
      prev = pend + 1;
    }
    at = end + 1;
  }
  *count = (uint8_t)n;
  return true;
}

static bool field_from(asf_field_t *f, const app_setting_decl_t *d) {
  memset(f, 0, sizeof(*f));
  if (!key_ok(d->key)) return false;
  size_t ln = d->label ? strnlen(d->label, ASF_LABEL_MAX + 1) : 0;
  if (ln == 0 || ln > ASF_LABEL_MAX || !text_ok(d->label, ln)) return false;
  switch (d->kind) {
  case APP_SETTING_TEXT:
  case APP_SETTING_SECRET:
    if (d->min < 0 || d->max < 1 || d->min > d->max || d->max > ASF_VALUE_MAX) return false;
    break;
  case APP_SETTING_NUMBER:
    if (d->min > d->max) return false;
    break;
  case APP_SETTING_CHOICE:
    if (!choices_ok(d->choices, &f->n_choices)) return false;
    memcpy(f->choices, d->choices, strlen(d->choices));
    break;
  default: return false;
  }
  memcpy(f->key, d->key, strlen(d->key));
  memcpy(f->label, d->label, ln);
  f->kind = (uint8_t)d->kind;
  f->min = d->min;
  f->max = d->max;
  return true;
}

int asf_declare(asf_form_t *form, const app_setting_decl_t *decls, uint32_t n) {
  if (!form || (n && !decls) || n > ASF_FIELDS_MAX) return APP_EINVAL;
  static asf_form_t next; // one app at a time; kept off the caller's stack (16 x 330 bytes)
  memset(&next, 0, sizeof(next));
  for (uint32_t i = 0; i < n; i++) {
    if (!field_from(&next.f[i], &decls[i])) return APP_EINVAL;
    for (uint32_t j = 0; j < i; j++) {
      if (!strcmp(next.f[j].key, next.f[i].key)) return APP_EINVAL;
    }
  }
  next.n = (int)n;
  *form = next;
  return 0;
}

int asf_find(const asf_form_t *form, const char *key) {
  if (!form || !key) return -1;
  for (int i = 0; i < form->n; i++) {
    if (!strcmp(form->f[i].key, key)) return i;
  }
  return -1;
}

bool asf_choice(const asf_field_t *f, int i, char *out, size_t out_len) {
  if (!f || f->kind != APP_SETTING_CHOICE || i < 0 || !out || !out_len) return false;
  const char *at = f->choices;
  for (int k = 0; k < i; k++) {
    at = strchr(at, '|');
    if (!at) return false;
    at++;
  }
  size_t n = strcspn(at, "|");
  if (n >= out_len) n = out_len - 1;
  memcpy(out, at, n);
  out[n] = '\0';
  return true;
}

static int say(char *why, size_t why_len, const char *what) {
  if (why && why_len) snprintf(why, why_len, "%s", what);
  return APP_EINVAL;
}

static int refuse(char *why, size_t why_len, const char *fmt, long v) {
  if (why && why_len) snprintf(why, why_len, fmt, v);
  return APP_EINVAL;
}

int asf_check(const asf_field_t *f, const char *value, size_t len, char *why, size_t why_len) {
  if (why && why_len) why[0] = '\0';
  if (!f || (!value && len)) return say(why, why_len, "no value");
  if (!value) value = "";
  if (memchr(value, '\0', len)) return say(why, why_len, "not text");
  switch (f->kind) {
  case APP_SETTING_TEXT:
  case APP_SETTING_SECRET:
    if (len > (size_t)f->max) return refuse(why, why_len, "too long (at most %ld bytes)", f->max);
    if (len < (size_t)f->min) return refuse(why, why_len, "too short (at least %ld)", f->min);
    if (!text_ok(value, len)) return say(why, why_len, "not text (control characters or broken UTF-8)");
    return 0;
  case APP_SETTING_NUMBER: {
    size_t i = value[0] == '-' ? 1 : 0;
    if (len == i || len - i > 10) return say(why, why_len, "not a whole number");
    long long v = 0;
    for (size_t k = i; k < len; k++) {
      if (value[k] < '0' || value[k] > '9') return say(why, why_len, "not a whole number");
      v = v * 10 + (value[k] - '0');
    }
    if (i) v = -v;
    if (v < f->min) return refuse(why, why_len, "at least %ld", f->min);
    if (v > f->max) return refuse(why, why_len, "at most %ld", f->max);
    return 0;
  }
  case APP_SETTING_CHOICE: {
    char c[ASF_CHOICES_BYTES + 1];
    for (int i = 0; asf_choice(f, i, c, sizeof(c)); i++) {
      if (strlen(c) == len && !memcmp(c, value, len)) return 0;
    }
    return say(why, why_len, "not one of the choices");
  }
  default: return say(why, why_len, "unknown field");
  }
}

size_t asf_shown(const asf_field_t *f, const void *stored, size_t stored_len, char *out, size_t out_len, bool *is_set) {
  if (is_set) *is_set = stored && stored_len > 0;
  if (!out || !out_len) return 0;
  out[0] = '\0';
  if (!f || !stored || !stored_len || f->kind == APP_SETTING_SECRET) return 0;
  size_t n = stored_len < out_len - 1 ? stored_len : out_len - 1;
  memcpy(out, stored, n);
  out[n] = '\0';
  return n;
}
