#pragma once
// serial_out (lib/trmnl/include/serial_out.h) on Arduino's Serial: answers that must arrive whole
#include <stddef.h>

// One formatted line (up to 255 bytes), paced into the TX ring; false when the host stopped reading before its end
bool serial_out_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
