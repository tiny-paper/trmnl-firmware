#include <string.h>
#include <touch_inject.h>

static touch_inject_ev_t ring[TOUCH_INJECT_MAX];
static volatile uint32_t head, tail; // written by the producer / by the consumer

bool touch_inject_push(uint8_t channel, uint8_t gesture) {
  if (channel > 2 || gesture > 2) return false;
  uint32_t h = head;
  if (h - tail >= TOUCH_INJECT_MAX) return false;
  ring[h % TOUCH_INJECT_MAX] = {channel, gesture};
  __atomic_store_n(&head, h + 1, __ATOMIC_RELEASE);
  return true;
}

int touch_inject_take(touch_inject_ev_t *out, int max) {
  int n = 0;
  while (n < max && tail != __atomic_load_n(&head, __ATOMIC_ACQUIRE)) {
    out[n++] = ring[tail % TOUCH_INJECT_MAX];
    __atomic_store_n(&tail, tail + 1, __ATOMIC_RELEASE);
  }
  return n;
}

void touch_inject_clear(void) {
  touch_inject_ev_t sink[TOUCH_INJECT_MAX];
  while (touch_inject_take(sink, TOUCH_INJECT_MAX)) {
  }
}

bool touch_inject_button(const char *button) {
  if (!button) return false;
  if (!strcmp(button, "left")) return touch_inject_push(0, 1);
  if (!strcmp(button, "middle")) return touch_inject_push(1, 1);
  if (!strcmp(button, "right")) return touch_inject_push(2, 1);
  if (!strcmp(button, "long") || !strcmp(button, "stop")) return touch_inject_push(1, 2);
  return false;
}
