// copyright (C) 2006 Marcin Slonicki <marcin@softservice.com.pl>
// copyright (C) 2007 Louis Frigon
// Copyright (C) 2015 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "peripheral.hpp"
#include "avclan_defs.h"
#include "avclan_frame.h"
#include "avclan_phy.h" // bus symbol I/O + transaction guard (target-provided)
#include "com232.h"     // error logging

#include <cstdint>

namespace avclan {

Peripheral::Error::Read Peripheral::read(AVCLAN_frame_t *in, log_t print) {
  struct errtype {
    Error::Read errno;
    union {
      uint8_t val; // BAD_LENGTH_RANGE: the out-of-range length value
      struct {
        uint8_t parity; // received (bad) parity bit
        uint16_t read_val;
      };
    };
  } err = {};

  using enum Error::Read;

  AVCLAN_stopEvent(); // quiesce contending sources during the read

  bool shouldACK = false;
  uint8_t tmp = 0, parity = 0;

  err.errno = Error::Read{AVCLAN_readstartbit()};
  if (static_cast<uint8_t>(err.errno))
    goto handle_err;

  AVCLAN_readbits<1>(&tmp);
  in->is_unicast = tmp;

  parity = AVCLAN_readbits<12>(&in->controller_addr);
  AVCLAN_readbits<1>(&tmp);
  if (parity != (tmp &= 1)) {
    err.errno = BAD_CONTROLLER_PARITY;
    if (print.verbose) {
      err.read_val = in->controller_addr;
      err.parity = tmp;
    }
    goto handle_err;
  }

  parity = AVCLAN_readbits<12>(&in->peripheral_addr);
  AVCLAN_readbits<1>(&tmp);
  if (parity != (tmp &= 1)) {
    err.errno = BAD_PERIPHERAL_PARITY;
    if (print.verbose) {
      err.read_val = in->peripheral_addr;
      err.parity = tmp;
    }
    goto handle_err;
  }

  shouldACK = !AVCLAN_ismuted() && (in->peripheral_addr == address);

  if (shouldACK)
    AVCLAN_sendbit_ACK();
  else
    AVCLAN_readbits<1>(&tmp);

  parity = AVCLAN_readbits<4>(&in->control);
  AVCLAN_readbits<1>(&tmp);
  if (parity != (tmp &= 1)) {
    err.errno = BAD_CONTROL_PARITY;
    if (print.verbose) {
      err.read_val = in->control;
      err.parity = tmp;
    }
    goto handle_err;
  } else if (shouldACK) {
    AVCLAN_sendbit_ACK();
  } else {
    AVCLAN_readbits<1>(&tmp);
  }

  parity = AVCLAN_readbyte(&in->length);
  AVCLAN_readbits<1>(&tmp);
  if (parity != (tmp &= 1)) {
    err.errno = BAD_LENGTH_PARITY;
    if (print.verbose) {
      err.read_val = in->length;
      err.parity = tmp;
    }
    goto handle_err;
  } else if (shouldACK) {
    AVCLAN_sendbit_ACK();
  } else {
    AVCLAN_readbits<1>(&tmp);
  }

  if (in->length == 0 || in->length > MAXMSGLEN) {
    err.errno = BAD_LENGTH_RANGE;
    err.val = in->length;
    goto handle_err;
  }

  for (uint8_t i = 0; i < in->length; i++) {
    parity = AVCLAN_readbits<8>(&in->data[i]);
    AVCLAN_readbits<1>(&tmp);
    if (parity != (tmp &= 1)) {
      err.errno = BAD_DATA_PARITY;
      if (print.verbose) {
        err.read_val = in->data[i];
        err.parity = tmp;
      }
      goto handle_err;
    } else if (shouldACK) {
      AVCLAN_sendbit_ACK();
    } else {
      AVCLAN_readbits<1>(&tmp);
    }
  }

  if (false) {
  handle_err:;
    AVCLAN_startEvent();
    RS232_Print("ERR(read): ");
    switch (err.errno) {
      case BAD_STARTBIT: RS232_Print("bad start bit (other)"); break;
      case STARTBIT_TOO_SHORT: RS232_Print("bad start bit (short)"); break;
      case STARTBIT_TOO_LONG: RS232_Print("bad start bit (long)"); break;
      case BAD_CONTROLLER_PARITY:
        RS232_Print("reading controller addr.");
        goto VERBOSE;
      case BAD_PERIPHERAL_PARITY:
        RS232_Print("reading peripheral addr.");
        goto VERBOSE;
      case BAD_CONTROL_PARITY: RS232_Print("reading control"); goto VERBOSE;
      case BAD_LENGTH_PARITY: RS232_Print("reading length"); goto VERBOSE;
      case BAD_LENGTH_RANGE:
        RS232_Print("bad length 0x");
        RS232_PrintHex4(err.val);
        break;
      case BAD_DATA_PARITY:
        RS232_Print("reading data");
        goto VERBOSE;
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
  if (print.print && (err.errno < STARTBIT_TOO_SHORT)) {
    if (err.errno > BAD_DATA_PARITY)
      in->length = 0;
    AVCLAN_printframe(in, print.binary);
  }

  return err.errno;
}

Peripheral::Error::Send Peripheral::send(const AVCLAN_frame_t *out,
                                         log_t print) {
  struct errtype {
    // Error enum is ordered such that a lower numeric value corresponds to
    // more success
    Error::Send errno;
    uint8_t val;
  } err = {};

  using enum Error::Send;

  avclan_bit_t parity;

  if (AVCLAN_ismuted()) {
    err.errno = MUTED;
    goto handle_err;
  }

  AVCLAN_stopEvent();

  if (!AVCLAN_sendstartbit()) {
    // Some other device is already driving the bus
    err.errno = BUSY;
    goto handle_err;
  }

  AVCLAN_sendbits<1>(static_cast<uint8_t>(out->is_unicast));

  parity = AVCLAN_sendbits<12>(out->controller_addr);
  AVCLAN_sendbit(parity);

  parity = AVCLAN_sendbits<12>(address);
  AVCLAN_sendbit(parity);

  if (out->is_unicast && !AVCLAN_readbit_ACK()) {
    err.errno = NAK_ADDRESS;
    goto handle_err;
  }

  parity = AVCLAN_sendbits<4>(out->control);
  AVCLAN_sendbit(parity);

  if (out->is_unicast && !AVCLAN_readbit_ACK()) {
    err.errno = NAK_CONTROL;
    goto handle_err;
  }

  parity = AVCLAN_sendbits<8>(out->length); // data length
  AVCLAN_sendbit(parity);

  if (out->is_unicast && !AVCLAN_readbit_ACK()) {
    err.errno = NAK_MESSAGE_LENGTH;
    goto handle_err;
  }

  for (uint8_t i = 0; i < out->length; i++) {
    parity = AVCLAN_sendbits<8>(out->data[i]);
    AVCLAN_sendbit(parity);
    // Based on the µPD6708 datasheet, ACK bit for broadcast doesn't seem
    // necessary (i.e. This deviates from the previous broadcast specific
    // function that sent an extra `1` bit after each byte/parity)
    if (out->is_unicast && !AVCLAN_readbit_ACK()) {
      err.errno = NAK_DATA;
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
      case MUTED: RS232_Print(": Device muted"); break;
      case BUSY: RS232_Print(": Busy bus"); break;
      case NAK_ADDRESS:
      case NAK_CONTROL:
      case NAK_MESSAGE_LENGTH:
      case NAK_DATA:
        RS232_Print(" NAK: ");
        switch (err.errno) {
          case NAK_ADDRESS: RS232_Print("address"); break;
          case NAK_CONTROL: RS232_Print("Control"); break;
          case NAK_MESSAGE_LENGTH: RS232_Print("Message length"); break;
          case NAK_DATA:
            RS232_Print(" data[");
            RS232_PrintDec(err.val);
            RS232_Print("]");
            break;
          case MUTED:
          case BUSY: __builtin_unreachable();
        }
        break;
    }
    RS232_Print("\n");
  } else {
    AVCLAN_startEvent();
  }

  if (print.print)
    AVCLAN_printframe(out, print.binary);

  return err.errno;
}

} // namespace avclan
