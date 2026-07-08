// copyright (C) 2006 Marcin Slonicki <marcin@softservice.com.pl>
// copyright (C) 2007 Louis Frigon
// Copyright (C) 2015 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "frame.hpp"

namespace {
using Error = avclan::detail::Error;
using enum Error::Parse;
} // namespace

namespace avclan {
void Frame::print(Frame::Print print) const {
  if (print.binary) {
    uint8_t buffer[8];
    uint8_t *bptr = buffer;
    *bptr++ = 0x10; // Data Link Escape, signaling binary data forthcoming
    *bptr++ = static_cast<uint8_t>(is_unicast);

    // Send addresses in big-endian order
    *bptr++ = static_cast<uint8_t>(controller_addr >> 8);
    *bptr++ = static_cast<uint8_t>(controller_addr);
    *bptr++ = static_cast<uint8_t>(peripheral_addr >> 8);
    *bptr++ = static_cast<uint8_t>(peripheral_addr);

    *bptr++ = control;
    *bptr++ = length;
    fwrite(buffer, 1, 8, stdout);
    fwrite(data, 1, length, stdout);

    bptr = buffer;
    *bptr++ = 0x17; // End of transmission block
    *bptr++ = 0x0A; // \n
    fwrite(buffer, 1, 2, stdout);
  } else {
    printf("%X", static_cast<unsigned>(is_unicast));
    printf(" 0x%03X", static_cast<unsigned>(controller_addr & 0x0FFF));
    printf(" 0x%03X", static_cast<unsigned>(peripheral_addr & 0x0FFF));
    printf(" 0x%X", static_cast<unsigned>(control & 0x0F));
    printf(" 0x%X", static_cast<unsigned>(length & 0x0F));

    for (uint8_t i = 0; i < length; i++) {
      printf(" 0x%02X", static_cast<unsigned>(data[i]));
    }
    putchar('\n');
  }
}

Error::Parse Frame::parse(const uint8_t *bytes, uint8_t len) {
  struct errtype {
    Error::Parse errno;
    uint8_t val;
  } err = {};

  const uint8_t *last = bytes + len;

  if (len < Frame::MIN_SIZE) {
    err.errno = TOO_SHORT;
    goto handle_err;
  }

  is_unicast = (*bytes++ != 0U);
  controller_addr = bytes[0] | ((uint16_t)bytes[1] << 8);
  bytes += 2;
  peripheral_addr = bytes[0] | ((uint16_t)bytes[1] << 8);
  bytes += 2;
  control = *bytes++;
  length = *bytes++;

  if (length > MAXLENGTH) {
    err.errno = LENGTH_TOO_BIG;
    err.val = length;
    goto handle_err;
  }

  if ((bytes + length) <= last) {
    memcpy(data, bytes, length);
  } else {
    err.errno = MISMATCH_LENGTH;
    goto handle_err;
  }

  if (false) { // NOLINT(readability-simplify-boolean-expr)
  handle_err:;
    fputs("ERR(parse): ", stdout);
    switch (err.errno) {
      case TOO_SHORT: puts("not enough bytes too fill AVCLAN frame"); break;
      case MISMATCH_LENGTH:
        puts("frame->length is longer than remaining data");
        break;
      case LENGTH_TOO_BIG:
        printf("frame->length exceeds MAXLENGTH: 0x%02X\n",
               static_cast<unsigned>(err.val));
        break;
      default: break;
    }
  }

  return err.errno;
}
} // namespace avclan
