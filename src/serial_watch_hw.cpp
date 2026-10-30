// The device side of lib/trmnl/include/serial_watch.h: samples HWCDC's TX ring, kicks its IN_EMPTY interrupt
#ifdef BOARD_TRMNL_X

#include "serial_watch_hw.h"

#include <Arduino.h>
#include <driver/usb_serial_jtag.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <hal/usb_serial_jtag_ll.h>
#include <serial_watch.h>
#include <trmnl_log.h>

#include "evlog.h"
#include "usb_port_role.h"

static TaskHandle_t task;

// What HWCDC's write() does when it judges the host connected: unmask IN_EMPTY and flush the FIFO. With room in the
// FIFO the ISR runs at once, sets `connected` and moves the ring into the FIFO.
static void kick(void) {
  usb_serial_jtag_ll_ena_intr_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
  usb_serial_jtag_ll_txfifo_flush();
}

static void watch(void *) {
  serial_watch_t w;
  serial_watch_init(&w);
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(SERIAL_WATCH_POLL_MS));
    // the PHY with the USB drive or keyboard host: no serial port to watch (usb_port_role.h)
    bool serial = usb_port_role() == USB_ROLE_SERIAL;
    int free = Serial.availableForWrite();
    serial_watch_act_t a = serial_watch_step(&w, millis(), free, serial && usb_serial_jtag_is_connected());
    if (a == SERIAL_WATCH_KICK) {
      // the second kick (2 s without progress) is a real stall; FIFO free = HWCDC's interrupt was masked (a kick
      // fixes that), FIFO full = the host takes no IN packets (its side)
      if (w.kicks == 2) {
        evlog_add("Serial: output stalled, %d bytes waiting, FIFO %s, IN ep1 state %u", w.max_free - free,
                  usb_serial_jtag_ll_txfifo_writable() ? "free" : "full", (unsigned)USB_SERIAL_JTAG.in_ep1_st.in_ep1_state);
      }
      kick();
    } else if (a == SERIAL_WATCH_RESUMED) {
      Log_info("Serial: output resumed after %u ms stalled, %u kicks", (unsigned)w.stall_ms, (unsigned)w.resumed_kicks);
      if (w.resumed_kicks >= 2) {
        evlog_add("Serial: output resumed after %u ms stalled, %u kicks", (unsigned)w.stall_ms,
                  (unsigned)w.resumed_kicks);
      }
    }
  }
}

void serial_watch_start(void) {
  if (task) return;
  if (xTaskCreatePinnedToCore(watch, "serial_watch", 3072, nullptr, 1, &task, 0) != pdPASS) {
    Log_error("Serial: watchdog task not started (out of memory)");
    task = nullptr;
  }
}

#endif // BOARD_TRMNL_X
