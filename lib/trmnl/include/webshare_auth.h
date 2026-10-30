#pragma once

#include <stdbool.h>
#include <stdint.h>

// Access code and sessions of the Wi-Fi page. No access without the 6-digit code shown on the X's screen.
// I/O free: the caller gives random numbers and the clock (millis, wraps are fine).

#define WS_PIN_LEN 6
#define WS_TOKEN_LEN 16 // hex characters
#define WS_LOGIN_TOKEN_LEN 32 // hex characters: 128 bits
#define WS_SESSIONS 4
#define WS_FAILS_PER_ROUND 5 // wrong codes before a lock
#define WS_LOCK_MS 60000u // first lock; doubles with each round
#define WS_MAX_FAILS 15 // wrong codes in all: the page is dead until the writer starts it again (new code)
#define WS_IDLE_MS (15u * 60u * 1000u)

typedef enum { WS_AUTH_OK, WS_AUTH_WRONG, WS_AUTH_LOCKED, WS_AUTH_DEAD } ws_auth_result_t;

typedef struct {
  char pin[WS_PIN_LEN + 1];
  int fails; // all wrong codes
  int round_fails; // since the last lock
  uint32_t locked_at;
  uint32_t lock_ms; // 0: not locked
  char tokens[WS_SESSIONS][WS_TOKEN_LEN + 1]; // "" for a free slot
  int next_slot;
  char login_token[WS_LOGIN_TOKEN_LEN + 1]; // one-time, "" when used or not set
  uint32_t token_at;
} ws_auth_t;

// A new code from `random` (a uniform 32-bit number; the bias of % 10^6 is below 0.03 %) and no sessions
void ws_auth_init(ws_auth_t *a, uint32_t random);

// Makes the one-time login token (for the link / QR on the X) from 4 random words; valid for one login and
// WS_IDLE_MS from `now`, gone at stop (the whole state is reset by ws_auth_init)
void ws_auth_new_token(ws_auth_t *a, const uint32_t random[4], uint32_t now);

// Logs in with the one-time token. OK only once: the token is used up at the first good login. A wrong or
// expired token counts as a wrong code (same lock). Session token as in ws_auth_login.
ws_auth_result_t ws_auth_token_login(ws_auth_t *a, const char *token, uint32_t now, const uint32_t token_random[2],
                                     char *token_out);

// Checks a code typed on the page. On OK, `token_random[2]` become a session token in `token_out`
// (WS_TOKEN_LEN + 1 bytes). The compare takes the same time for every wrong code. While locked or dead nothing is
// compared, not even the right code.
ws_auth_result_t ws_auth_login(ws_auth_t *a, const char *guess, uint32_t now, const uint32_t token_random[2],
                               char *token_out);

// Milliseconds a locked page still waits (0 if not locked)
uint32_t ws_auth_lock_left(const ws_auth_t *a, uint32_t now);

// Is this the token of a live session?
bool ws_auth_session_ok(const ws_auth_t *a, const char *token);

// One request to a page that curl can call without a cookie (POST /apps?t=<token> or ?code=<6 digits>): allowed with a
// live session cookie, else with the one-time token (used up), else with the code (as often as needed). Every other
// case is refused, and a wrong token or code counts like any wrong code (lock, dead). `session` is the cookie value,
// the others the query parameters; each may be nullptr or empty.
bool ws_auth_request(ws_auth_t *a, const char *session, const char *token, const char *code, uint32_t now,
                     const uint32_t random[2], char *session_out);

// Idle timer: `last` is the millis of the last request (or the start)
bool ws_idle_expired(uint32_t last, uint32_t now);
