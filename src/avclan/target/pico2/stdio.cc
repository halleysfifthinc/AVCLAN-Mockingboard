
#include <cerrno>
#include <cstdio>
#include <unistd.h>

#include "hal/stdio.h"
#include "pico/stdio.h"
#include "pico/stdio_usb.h"
#include "pico/time.h"
#include "tusb.h" // IWYU pragma: keep

// stdio_write_nonblock() is all-or-nothing. The CDC TX FIFO must hold the
// largest buffer the app writes; that is a text frame log line (Frame::print),
// ~186 bytes at MAXLENGTH=32.
static_assert(CFG_TUD_CDC_TX_BUFSIZE >= 256,
              "CDC TX FIFO too small to hold a whole frame log line");

namespace {
// Set when a dropped buffer had no room for its indicator; the next call emits
// it. The AVR port instead overwrites the last three queued bytes, which
// collapses a burst of drops to a single '!' line in exactly the same way.
bool drop_indicator_pending = false;

// stdout's only backend here is the usb driver, which writes into the same CDC
// TX FIFO measured below, so printf and this share one stream and one order.
void queue(const char *str, int len) {
  stdio_put_string(str, len, false, false);
}
} // namespace

extern "C" void stdio_init() {
  stdio_usb_init();
  stdio_set_translate_crlf(&stdio_usb, false);
  // pico_stdio wraps printf/puts/putchar straight onto the CDC, but not
  // fputs/fwrite. Unbuffered stdout keeps the newlib path in step with them
  // instead of stranding whole strings in the FILE buffer.
  setvbuf(stdout, nullptr, _IONBF, 0);
}

// Overrides the SDK's weak newlib hook, which waits forever. EAGAIN rather than
// a 0-length read: newlib's refill skips a stream that has ever seen EOF.
extern "C" int _read(int handle, char *buffer, int length) {
  if (handle != STDIN_FILENO) {
    errno = EBADF;
    return -1;
  }

  const int count = stdio_get_until(buffer, length, make_timeout_time_us(0));
  if (count < 0) {
    errno = EAGAIN;
    return -1;
  }

  return count;
}

extern "C" bool stdio_write_nonblock(const void *buf, uint8_t len) {
  uint32_t avail = tud_cdc_write_available();

  if (drop_indicator_pending) {
    if (avail < 3)
      return false;
    queue("!\n", 2);
    drop_indicator_pending = false;
    avail -= 2;
  }

  if (len == 0)
    return true;

  if (len <= avail) {
    queue(static_cast<const char *>(buf), len);
    return true;
  }

  // Every buffer ends with '\n', so the FIFO already sits at a line boundary.
  if (avail >= 3)
    queue("!\n", 2);
  else
    drop_indicator_pending = true;

  return false;
}
