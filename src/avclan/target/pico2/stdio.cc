
#include "hal/stdio.h"
#include "pico/stdio_usb.h"

extern "C" void stdio_init() { stdio_usb_init(); }

extern "C" bool stdio_write_nonblock(const void *buf, uint8_t len) {
  return false;
}
