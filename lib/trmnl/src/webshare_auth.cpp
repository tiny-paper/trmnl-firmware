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

void ws_auth_new_token(ws_auth_t *a, const uint32_t r[4], uint32_t now) {
  snprintf(a->login_token, sizeof(a->login_token), "%08x%08x%08x%08x", (unsigned)r[0], (unsigned)r[1], (unsigned)r[2],
           (unsigned)r[3]);
  a->token_at = now;
}

static ws_auth_result_t open_session(ws_auth_t *a, const uint32_t token_random[2], char *token_out) {
  char *slot = a->tokens[a->next_slot];
  a->next_slot = (a->next_slot + 1) % WS_SESSIONS; // the oldest session goes first
  snprintf(slot, WS_TOKEN_LEN + 1, "%08x%08x", (unsigned)token_random[0], (unsigned)token_random[1]);
  if (token_out) memcpy(token_out, slot, WS_TOKEN_LEN + 1);
  return WS_AUTH_OK;
}

static ws_auth_result_t fail(ws_auth_t *a, uint32_t now) {
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

ws_auth_result_t ws_auth_token_login(ws_auth_t *a, const char *token, uint32_t now, const uint32_t token_random[2],
                                     char *token_out) {
  if (a->fails >= WS_MAX_FAILS) return WS_AUTH_DEAD;
  if (ws_auth_lock_left(a, now)) return WS_AUTH_LOCKED;
  bool live = a->login_token[0] && (uint32_t)(now - a->token_at) < WS_IDLE_MS;
  if (!live || !same(a->login_token, token, WS_LOGIN_TOKEN_LEN)) return fail(a, now);
  a->login_token[0] = '\0'; // used up
  a->round_fails = 0;
  return open_session(a, token_random, token_out);
}

ws_auth_result_t ws_auth_login(ws_auth_t *a, const char *guess, uint32_t now, const uint32_t token_random[2],
                               char *token_out) {
  if (a->fails >= WS_MAX_FAILS) return WS_AUTH_DEAD;
  if (ws_auth_lock_left(a, now)) return WS_AUTH_LOCKED;
  if (!same(a->pin, guess, WS_PIN_LEN)) return fail(a, now);
  a->round_fails = 0;
  return open_session(a, token_random, token_out);
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

bool ws_auth_request(ws_auth_t *a, const char *session, const char *token, const char *code, uint32_t now,
                     const uint32_t random[2], char *session_out) {
  if (session_out) session_out[0] = '\0';
  if (session && session[0] && ws_auth_session_ok(a, session)) return true;
  if (token && token[0]) return ws_auth_token_login(a, token, now, random, session_out) == WS_AUTH_OK;
  if (code && code[0]) return ws_auth_login(a, code, now, random, session_out) == WS_AUTH_OK;
  return false;
}
