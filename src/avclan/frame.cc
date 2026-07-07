// copyright (C) 2006 Marcin Slonicki <marcin@softservice.com.pl>
// copyright (C) 2007 Louis Frigon
// Copyright (C) 2015 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdint>
#include <cstring>

#include "com232.h" // error logging
#include "frame.hpp"

namespace {
using Error = avclan::Frame::Error;
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
    RS232_sendbytes(buffer, 8);
    RS232_sendbytes(data, length);

    bptr = buffer;
    *bptr++ = 0x17; // End of transmission block
    *bptr++ = 0x0D; // \r
    *bptr++ = 0x0A; // \n
    RS232_sendbytes(buffer, 3);
  } else {
    RS232_PrintHex4(static_cast<uint8_t>(is_unicast));

    RS232_Print(" 0x");
    RS232_PrintHex12(controller_addr);
    RS232_Print(" 0x");
    RS232_PrintHex12(peripheral_addr);

    RS232_Print(" 0x");
    RS232_PrintHex4(control);

    RS232_Print(" 0x");
    RS232_PrintHex4(length);

    for (uint8_t i = 0; i < length; i++) {
      RS232_Print(" 0x");
      RS232_PrintHex8(data[i]);
    }
    RS232_Print("\n");
  }
}

Error::Parse Frame::parse(const uint8_t *bytes, uint8_t len) {
  struct errtype {
    Error::Parse errno;
    uint8_t val;
  } err = {};

  const uint8_t *last = bytes + len;

  if (len < sizeof(avclan::Frame)) {
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
    RS232_Print("ERR(parse): ");
    switch (err.errno) {
      case TOO_SHORT:
        RS232_Print("not enough bytes too fill AVCLAN frame");
        break;
      case MISMATCH_LENGTH:
        RS232_Print("frame->length is longer than remaining data");
        break;
      case LENGTH_TOO_BIG:
        RS232_Print("frame->length exceeds MAXLENGTH: 0x");
        RS232_PrintHex8(err.val);
        break;
      default: break;
    }
    RS232_Print("\n");
  }

  return err.errno;
}
} // namespace avclan
