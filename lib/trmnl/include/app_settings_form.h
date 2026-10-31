#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <app_api.h>

// An app's settings as a phone form (API 2.6, docs/apps.md "Settings profile"): the pure rules. The app declares its
// fields once (settings_declare); the firmware keeps a copy here, the Wi-Fi page shows the form, and every value the
// phone sends is checked against its field before setting_set writes it. A secret is never shown back or logged.

#define ASF_FIELDS_MAX 16
#define ASF_KEY_MAX 15 // as setting_get/set (NVS)
#define ASF_LABEL_MAX 48
#define ASF_VALUE_MAX 256 // setting_set's limit
#define ASF_CHOICES_MAX 16
#define ASF_CHOICES_BYTES 256 // "a|b|c" in all
#define ASF_WHY_MAX 80

typedef struct {
  char key[ASF_KEY_MAX + 1];
  char label[ASF_LABEL_MAX + 1];
  uint8_t kind; // app_setting_kind_t
  uint8_t n_choices;
  int32_t min, max;
  char choices[ASF_CHOICES_BYTES + 1]; // the app's "a|b|c", kept as given
} asf_field_t;

typedef struct {
  asf_field_t f[ASF_FIELDS_MAX];
  int n;
} asf_form_t;

// Copies and checks the app's declaration: 0, or APP_EINVAL (nothing kept) for decls NULL with n > 0, n over
// ASF_FIELDS_MAX, a key that is empty, too long, not [A-Za-z0-9_] or used twice; a label empty, too long or with control
// characters; an unknown kind; text/secret limits outside 0 <= min <= max <= ASF_VALUE_MAX or max 0; number min > max;
// choice without choices, an empty or repeated choice, more than ASF_CHOICES_MAX or ASF_CHOICES_BYTES. n 0 clears
int asf_declare(asf_form_t *form, const app_setting_decl_t *decls, uint32_t n);

// The field of `key`, or -1
int asf_find(const asf_form_t *form, const char *key);

// Choice i of a choice field (no '|'), into out; false past the last
bool asf_choice(const asf_field_t *f, int i, char *out, size_t out_len);

// Checks a value the phone sent (`len` bytes, need not end in NUL): 0, or APP_EINVAL with `why` the page's message
// ("too long (at most 32)", "not a whole number", "at least 1", "not one of the choices", ...). The value is never
// copied into `why`. Text and secrets: min..max bytes of UTF-8 without control characters. Numbers: optional '-' and
// digits, in min..max. Choices: exactly one of them. An empty value is allowed when min is 0 (text, secret) and
// clears the setting
int asf_check(const asf_field_t *f, const char *value, size_t len, char *why, size_t why_len);

// What the page may show of a stored value (`stored`, `stored_len` bytes; NULL or 0: not set): text, number and choice
// as they are, a secret never (out ""). *is_set says whether there is a value. Returns the bytes put in out
size_t asf_shown(const asf_field_t *f, const void *stored, size_t stored_len, char *out, size_t out_len, bool *is_set);
