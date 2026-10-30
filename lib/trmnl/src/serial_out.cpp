#include "serial_out.h"

size_t serial_out_write(const serial_out_port_t *p, const uint8_t *b, size_t n, uint32_t stall_ms) {
  size_t done = 0;
  uint32_t since = p->now_ms(p->ctx);
  while (done < n) {
    int room = p->room(p->ctx);
    if (room > 0) {
      size_t want = n - done < (size_t)room ? n - done : (size_t)room;
      size_t took = p->write(p->ctx, b + done, want);
      if (took > want) took = want;
      if (took) {
        done += took;
        since = p->now_ms(p->ctx);
        continue;
      }
    }
    if ((uint32_t)(p->now_ms(p->ctx) - since) >= stall_ms) break;
    p->wait_ms(p->ctx, 1);
  }
  return done;
}
