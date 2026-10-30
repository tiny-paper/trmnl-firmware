#include "webshare_auth.h"

#include <stdio.h>
#include <string.h>

void ws_auth_init(ws_auth_t *a, uint32_t random) {
  memset(a, 0, sizeof(*a));
  snprintf(a->pin, sizeof(a->pin), "%06u", (unsigned)(random % 1000000u));
}

// Equal-time compare over the full length of `a`; also false when the lengths differ
static bool same(const char *a, const char *b, size_t len) {
  if (!a || !b || strlen(b) != len) return false;
  unsigned char diff = 0;
  for (size_t i = 0; i < len; i++) diff |= (unsigned char)(a[i] ^ b[i]);
  return diff == 0;
}

uint32_t ws_auth_lock_left(const ws_auth_t *a, uint32_t now) {
  if (!a->lock_ms) return 0;
  uint32_t passed = now - a->locked_at;
  return passed >= a->lock_ms ? 0 : a->lock_ms - passed;
}

ws_auth_result_t ws_auth_login(ws_auth_t *a, const char *guess, uint32_t now, const uint32_t token_random[2],
                               char *token_out) {
  if (a->fails >= WS_MAX_FAILS) return WS_AUTH_DEAD;
  if (ws_auth_lock_left(a, now)) return WS_AUTH_LOCKED;
  if (!same(a->pin, guess, WS_PIN_LEN)) {
    a->fails++;
    if (a->fails >= WS_MAX_FAILS) return WS_AUTH_DEAD;
    if (++a->round_fails >= WS_FAILS_PER_ROUND) {
      a->round_fails = 0;
      a->lock_ms = a->lock_ms ? a->lock_ms * 2 : WS_LOCK_MS;
      a->locked_at = now;
      return WS_AUTH_LOCKED;
    }
    return WS_AUTH_WRONG;
  }
  a->round_fails = 0;
  char *slot = a->tokens[a->next_slot];
  a->next_slot = (a->next_slot + 1) % WS_SESSIONS; // the oldest session goes first
  snprintf(slot, WS_TOKEN_LEN + 1, "%08x%08x", (unsigned)token_random[0], (unsigned)token_random[1]);
  if (token_out) memcpy(token_out, slot, WS_TOKEN_LEN + 1);
  return WS_AUTH_OK;
}

bool ws_auth_session_ok(const ws_auth_t *a, const char *token) {
  if (!token) return false;
  bool ok = false;
  for (int i = 0; i < WS_SESSIONS; i++) {
    if (a->tokens[i][0] && same(a->tokens[i], token, WS_TOKEN_LEN)) ok = true;
  }
  return ok;
}

bool ws_idle_expired(uint32_t last, uint32_t now) { return (uint32_t)(now - last) >= WS_IDLE_MS; }
