// copyright (C) 2006 Marcin Slonicki <marcin@softservice.com.pl>
// copyright (C) 2007 Louis Frigon
// Copyright (C) 2015 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdint.h>
#include <string.h>

#include "avclan_frame.h"
#include "avclan_phy.h" // bus symbol I/O + transaction guard (target-provided)
#include "com232.h"     // error logging

void AVCLAN_printframe(const AVCLAN_frame_t *frame, bool binary) {
  if (binary) {
    uint8_t buffer[8];
    buffer[0] = 0x10; // Data Link Escape, signaling binary data forthcoming
    buffer[1] = frame->is_unicast;

    // Send addresses in big-endian order
    buffer[2] = (uint8_t)(frame->controller_addr >> 8);
    buffer[3] = (uint8_t)frame->controller_addr;
    buffer[4] = (uint8_t)(frame->peripheral_addr >> 8);
    buffer[5] = (uint8_t)frame->peripheral_addr;

    buffer[6] = frame->control;
    buffer[7] = frame->length;
    RS232_sendbytes((uint8_t *)&buffer, 8);
    RS232_sendbytes(frame->data, frame->length);

    buffer[0] = 0x17; // End of transmission block
    buffer[1] = 0x0D; // \r
    buffer[2] = 0x0A; // \n
    RS232_sendbytes((uint8_t *)&buffer, 3);
  } else {
    RS232_PrintHex4(frame->is_unicast);

    RS232_Print(" 0x");
    RS232_PrintHex12(frame->controller_addr);
    RS232_Print(" 0x");
    RS232_PrintHex12(frame->peripheral_addr);

    RS232_Print(" 0x");
    RS232_PrintHex4(frame->control);

    RS232_Print(" 0x");
    RS232_PrintHex4(frame->length);

    for (uint8_t i = 0; i < frame->length; i++) {
      RS232_Print(" 0x");
      RS232_PrintHex8(frame->data[i]);
    }
    RS232_Print("\n");
  }
}

uint8_t AVCLAN_parseframe(const uint8_t *bytes, uint8_t len,
                          AVCLAN_frame_t *frame) {
  struct errtype {
    enum : uint8_t {
      TOO_SHORT = 0x01,
      MISMATCH_LENGTH,
      LENGTH_TOO_BIG,
    } errno;
    uint8_t val;
  } err = {0};

  if (len < sizeof(AVCLAN_frame_t)) {
    err.errno = TOO_SHORT;
    goto handle_err;
  }
  const uint8_t *last = bytes + len;

  frame->is_unicast = *bytes++;
  frame->controller_addr = bytes[0] | ((uint16_t)bytes[1] << 8);
  bytes += 2;
  frame->peripheral_addr = bytes[0] | ((uint16_t)bytes[1] << 8);
  bytes += 2;
  frame->control = *bytes++;
  frame->length = *bytes++;

  if (frame->length > MAXMSGLEN) {
    err.errno = LENGTH_TOO_BIG;
    err.val = frame->length;
    goto handle_err;
  }

  if ((bytes + frame->length) <= last) {
    memcpy(frame->data, bytes, frame->length);
  } else {
    err.errno = MISMATCH_LENGTH;
    goto handle_err;
  }

  if (false) {
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
        RS232_Print("frame->length exceeds MAXMSGLEN: 0x");
        RS232_PrintHex8(err.val);
        break;
      default: break;
    }
    RS232_Print("\n");
  }

  return err.errno;
}
