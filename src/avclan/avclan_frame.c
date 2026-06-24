// copyright (C) 2006 Marcin Slonicki <marcin@softservice.com.pl>
// copyright (C) 2007 Louis Frigon
// Copyright (C) 2015 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdint.h>
#include <string.h>

#include "avclan_frame.h"
#include "avclan_phy.h" // bus symbol I/O + transaction guard (target-provided)
#include "com232.h"     // error logging

avclan_readerr_t AVCLAN_readframe(AVCLAN_frame_t *frame, log_t print) {
  struct errtype {
    avclan_readerr_t errno;
    union {
      uint8_t val; // BAD_LENGTH_RANGE: the out-of-range length value
      struct {
        uint16_t read_val;
        uint8_t parity; // received (bad) parity bit
      };
    };
  } err = {0};

  AVCLAN_stopEvent(); // quiesce contending sources during the read

  uint8_t tmp = 0;

  err.errno = AVCLAN_readstartbit();
  if (err.errno)
    goto handle_err;

  AVCLAN_readbits(&tmp, 1);
  frame->is_unicast = tmp;

  uint8_t parity = AVCLAN_readbits(&frame->controller_addr, 12);
  AVCLAN_readbits(&tmp, 1);
  if (parity != (tmp &= 1)) {
    err.errno = rBAD_CONTROLLER_PARITY;
    if (print.verbose) {
      err.read_val = frame->controller_addr;
      err.parity = tmp;
    }
    goto handle_err;
  }

  parity = AVCLAN_readbits(&frame->peripheral_addr, 12);
  AVCLAN_readbits(&tmp, 1);
  if (parity != (tmp &= 1)) {
    err.errno = rBAD_PERIPHERAL_PARITY;
    if (print.verbose) {
      err.read_val = frame->peripheral_addr;
      err.parity = tmp;
    }
    goto handle_err;
  }

  bool shouldACK = !AVCLAN_ismuted() && (frame->peripheral_addr == DEVICE_ADDR);

  if (shouldACK)
    AVCLAN_sendbit_ACK();
  else
    AVCLAN_readbits(&tmp, 1);

  parity = AVCLAN_readbits(&frame->control, 4);
  AVCLAN_readbits(&tmp, 1);
  if (parity != (tmp &= 1)) {
    err.errno = rBAD_CONTROL_PARITY;
    if (print.verbose) {
      err.read_val = frame->control;
      err.parity = tmp;
    }
    goto handle_err;
  } else if (shouldACK) {
    AVCLAN_sendbit_ACK();
  } else {
    AVCLAN_readbits(&tmp, 1);
  }

  parity = AVCLAN_readbyte(&frame->length);
  AVCLAN_readbits(&tmp, 1);
  if (parity != (tmp &= 1)) {
    err.errno = rBAD_LENGTH_PARITY;
    if (print.verbose) {
      err.read_val = frame->length;
      err.parity = tmp;
    }
    goto handle_err;
  } else if (shouldACK) {
    AVCLAN_sendbit_ACK();
  } else {
    AVCLAN_readbits(&tmp, 1);
  }

  if (frame->length == 0 || frame->length > MAXMSGLEN) {
    err.errno = rBAD_LENGTH_RANGE;
    err.val = frame->length;
    goto handle_err;
  }

  for (uint8_t i = 0; i < frame->length; i++) {
    parity = AVCLAN_readbyte(&frame->data[i]);
    AVCLAN_readbits(&tmp, 1);
    if (parity != (tmp &= 1)) {
      err.errno = rBAD_DATA_PARITY;
      if (print.verbose) {
        err.read_val = frame->data[i];
        err.parity = tmp;
      }
      goto handle_err;
    } else if (shouldACK) {
      AVCLAN_sendbit_ACK();
    } else {
      AVCLAN_readbits(&tmp, 1);
    }
  }

  if (false) {
  handle_err:;
    AVCLAN_startEvent();
    RS232_Print("ERR(read): ");
    switch (err.errno) {
      case rLATCHED_COMPARATOR: RS232_Print("latched comparator"); break;
      case rSTARTBIT_TOO_SHORT: RS232_Print("start bit too short"); break;
      case rSTARTBIT_TOO_LONG: RS232_Print("start bit too long"); break;
      case rBAD_CONTROLLER_PARITY:
        RS232_Print("reading controller addr.");
        goto VERBOSE;
      case rBAD_PERIPHERAL_PARITY:
        RS232_Print("reading peripheral addr.");
        goto VERBOSE;
      case rBAD_CONTROL_PARITY: RS232_Print("reading control"); goto VERBOSE;
      case rBAD_LENGTH_PARITY: RS232_Print("reading length"); goto VERBOSE;
      case rBAD_LENGTH_RANGE:
        RS232_Print("bad length 0x");
        RS232_PrintHex4(err.val);
        break;
      case rBAD_DATA_PARITY: RS232_Print("reading data"); goto VERBOSE;
      case rNO_ERROR:
        __builtin_unreachable();
      VERBOSE:
        if (print.verbose) {
          RS232_Print("; read 0x");
          RS232_PrintHex(err.read_val);
          RS232_Print(" and got bad parity ");
          RS232_PrintHex4(err.parity);
        }
    }
    RS232_Print("\n");
  } else {
    AVCLAN_startEvent();
  }

  // Only print if some data has been correctly received
  if (print.print && (err.errno < rSTARTBIT_TOO_SHORT)) {
    if (err.errno > rBAD_DATA_PARITY)
      frame->length = 0;
    AVCLAN_printframe(frame, print.binary);
  }

  return err.errno;
}

avclan_senderr_t AVCLAN_sendframe(const AVCLAN_frame_t *frame, log_t print) {
  struct errtype {
    // Error enum is ordered such that a lower numeric value corresponds to more
    // success
    avclan_senderr_t errno;
    uint8_t val;
  } err = {0};

  if (AVCLAN_ismuted()) {
    err.errno = sMUTED;
    goto handle_err;
  }

  AVCLAN_stopEvent();

  if (!AVCLAN_sendstartbit()) {
    // Some other device is already driving the bus
    err.errno = sBUSY;
    goto handle_err;
  }

  AVCLAN_sendbits(&(uint8_t){frame->is_unicast}, 1);

  avclan_bit_t parity = AVCLAN_sendbits(&frame->controller_addr, 12);
  AVCLAN_sendbit(parity);

  parity = AVCLAN_sendbits(&frame->peripheral_addr, 12);
  AVCLAN_sendbit(parity);

  if (frame->is_unicast && !AVCLAN_readbit_ACK()) {
    err.errno = sNAK_ADDRESS;
    goto handle_err;
  }

  parity = AVCLAN_sendbits(&frame->control, 4);
  AVCLAN_sendbit(parity);

  if (frame->is_unicast && !AVCLAN_readbit_ACK()) {
    err.errno = sNAK_CONTROL;
    goto handle_err;
  }

  parity = AVCLAN_sendbyte(&frame->length); // data length
  AVCLAN_sendbit(parity);

  if (frame->is_unicast && !AVCLAN_readbit_ACK()) {
    err.errno = sNAK_MESSAGE_LENGTH;
    goto handle_err;
  }

  for (uint8_t i = 0; i < frame->length; i++) {
    parity = AVCLAN_sendbyte(&frame->data[i]);
    AVCLAN_sendbit(parity);
    // Based on the µPD6708 datasheet, ACK bit for broadcast doesn't seem
    // necessary (i.e. This deviates from the previous broadcast specific
    // function that sent an extra `1` bit after each byte/parity)
    if (frame->is_unicast && !AVCLAN_readbit_ACK()) {
      err.errno = sNAK_DATA;
      err.val = i;
      goto handle_err;
    }
    // else
    //   AVCLAN_sendbit_1();
  }

  // back to read mode
  if (false) {
  handle_err:;
    AVCLAN_startEvent();
    RS232_Print("Error");
    switch (err.errno) {
      case sMUTED: RS232_Print(": Device muted"); break;
      case sBUSY: RS232_Print(": Busy bus"); break;
      case sNAK_ADDRESS:
      case sNAK_CONTROL:
      case sNAK_MESSAGE_LENGTH:
      case sNAK_DATA:
        RS232_Print(" NAK: ");
        switch (err.errno) {
          case sNAK_ADDRESS: RS232_Print("address"); break;
          case sNAK_CONTROL: RS232_Print("Control"); break;
          case sNAK_MESSAGE_LENGTH: RS232_Print("Message length"); break;
          case sNAK_DATA:
            RS232_Print(" data[");
            RS232_PrintDec(err.val);
            RS232_Print("]");
            break;
          case sNO_ERROR:
          case sMUTED:
          case sBUSY: __builtin_unreachable();
        }
        break;
      case sNO_ERROR: __builtin_unreachable();
    }
    RS232_Print("\n");
  } else {
    AVCLAN_startEvent();
  }

  if (print.print)
    AVCLAN_printframe(frame, print.binary);

  return err.errno;
}

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
