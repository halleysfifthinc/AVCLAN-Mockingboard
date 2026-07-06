// copyright (C) 2006 Marcin Slonicki <marcin@softservice.com.pl>
// copyright (C) 2007 Louis Frigon
// Copyright (C) 2015 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

/*
        AVC LAN Frame Format
    │ Bits │ Description
    ────────────────────────────────────────
    |  1   │ Start bit
    |  1   │ Direct/broadcast
    |  12  │ Controller address
    |  1   │ Parity
    |  12  │ Peripheral address
    |  1   │ Parity
    |  1   │ *Acknowledge* (read below)
    |  4   │ Control
    |  1   │ Parity
    |  1   │ *Acknowledge*
    |  8   │ Message length (n)
    |  1   │ Parity
    |  1   │ *Acknowledge*
    ────────
       | 8 │ Data
       | 1 │ Parity
       | 1 │ *Acknowledge*
       *repeat `n` times*

  No acknowledge bits are sent for broadcast frames.
*/

#include "bus.hpp"
#include "avclan.hpp"
#include "avclan_defs.h"
#include "avclan_phy.h" // bridge until phy has been ported
#include "com232.h"
#include "frame.hpp"

namespace {
constexpr int ADDR_WIDTH = 12;
constexpr int CONTROL_WIDTH = 4;
constexpr int BYTE_WIDTH = 8;
} // namespace

namespace avclan {

Bus::Handle::Handle() { AVCLAN_stopEvent(); }
Bus::Handle::~Handle() { AVCLAN_startEvent(); }

void Bus::init() { AVCLAN_busInit(); };
void Bus::mute(bool mute) { AVCLAN_muteDevice(mute); };
bool Bus::is_muted() const { return AVCLAN_ismuted(); };

auto Bus::read(uint16_t address, Frame *in, Frame::Print print) -> Error::Read {
  struct errtype {
    Error::Read errno;
    uint16_t val;
  } err = {};

  using enum detail::Error::Read;

  { // bound handle lifetime
    auto handle = get();

    bool shouldACK = false;
    uint8_t tmp = 0;

    err.errno = handle.readstartbit();
    if (err.errno != Error::Read{0})
      goto handle_err;

    handle.read<1>(&tmp, no_parity);
    in->is_unicast = (tmp != 0U);

    if (auto rerr = handle.read<ADDR_WIDTH>(&in->controller_addr, with_parity);
        rerr == BAD_PARITY) {
      err.errno = BAD_CONTROLLER_PARITY;
      if (print.verbose) {
        err.val = in->controller_addr;
      }
      goto handle_err;
    }

    if (auto rerr = handle.read<ADDR_WIDTH>(
            &in->peripheral_addr, with_ack,
            // Using lambda for delayed evaluation of peripheral_addr field
            // deref, which will be written by the time the lambda is evaluated
            [&]() {
              shouldACK = !is_muted() && (in->peripheral_addr == address);
              return shouldACK;
            });
        rerr == BAD_PARITY) {
      err.errno = BAD_PERIPHERAL_PARITY;
      if (print.verbose) {
        err.val = in->peripheral_addr;
      }
      goto handle_err;
    }

    if (auto rerr =
            handle.read<CONTROL_WIDTH>(&in->control, with_ack, shouldACK);
        rerr == BAD_PARITY) {
      err.errno = BAD_CONTROL_PARITY;
      if (print.verbose) {
        err.val = in->control;
      }
      goto handle_err;
    }

    if (auto rerr = handle.read<BYTE_WIDTH>(&in->length, with_ack, shouldACK);
        rerr == BAD_PARITY) {
      err.errno = BAD_LENGTH_PARITY;
      if (print.verbose) {
        err.val = in->length;
      }
      goto handle_err;
    }

    if (in->length == 0 || in->length > Frame::MAXLENGTH) {
      err.errno = BAD_LENGTH_RANGE;
      err.val = in->length;
      goto handle_err;
    }

    for (uint8_t i = 0; i < in->length; i++) {
      if (auto rerr =
              handle.read<BYTE_WIDTH>(&in->data[i], with_ack, shouldACK);
          rerr == BAD_PARITY) {
        err.errno = BAD_DATA_PARITY;
        if (print.verbose) {
          err.val = in->data[i];
        }
        goto handle_err;
      }
    }
  } // destroy handle

  if (false) { // NOLINT(readability-simplify-boolean-expr)
  handle_err:;
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
      case BAD_DATA_PARITY: RS232_Print("reading data"); goto VERBOSE;
      case BAD_PARITY:
        __builtin_unreachable();
      VERBOSE:
        if (print.verbose) {
          RS232_Print("; read 0x");
          RS232_PrintHex(err.val);
        }
    }
    RS232_Print("\n");
  }

  // Only print if some data has been correctly received
  if (print.print && (err.errno < STARTBIT_TOO_SHORT)) {
    if (err.errno > BAD_DATA_PARITY)
      in->length = 0;
    in->print(print);
  }

  return err.errno;
}

auto Bus::send(const Frame *out, Frame::Print print) -> Error::Send {
  struct errtype {
    // Error enum is ordered such that a lower numeric value corresponds to
    // more success
    Error::Send errno;
    uint8_t val;
  } err = {};

  using enum detail::Error::Send;

  if (is_muted()) {
    err.errno = MUTED;
    goto handle_err;
  }

  { // bound handle lifetime
    auto handle = get();

    if (!handle.sendstartbit()) {
      // Some other device is already driving the bus
      err.errno = BUSY;
      goto handle_err;
    }

    handle.send<1>(static_cast<uint8_t>(out->is_unicast), no_parity);

    handle.send<ADDR_WIDTH>(out->controller_addr, with_parity);

    if (auto serr = handle.send<ADDR_WIDTH>(out->peripheral_addr, with_ack,
                                            out->is_unicast);
        serr == NAK) {
      err.errno = NAK_ADDRESS;
      goto handle_err;
    }

    if (auto serr =
            handle.send<CONTROL_WIDTH>(out->control, with_ack, out->is_unicast);
        serr == NAK) {
      err.errno = NAK_CONTROL;
      goto handle_err;
    }

    if (auto serr =
            handle.send<BYTE_WIDTH>(out->length, with_ack, out->is_unicast);
        serr == NAK) {
      err.errno = NAK_MESSAGE_LENGTH;
      goto handle_err;
    }

    for (uint8_t i = 0; i < out->length; i++) {
      // Based on the µPD6708 datasheet, ACK bit for broadcast doesn't seem
      // necessary (i.e. This deviates from the previous broadcast specific
      // function that sent an extra `1` bit after each byte/parity)
      // Explanation for why audio-group broadcast state report isn't working?
      if (auto serr =
              handle.send<BYTE_WIDTH>(out->data[i], with_ack, out->is_unicast);
          serr == NAK) {
        err.errno = NAK_DATA;
        err.val = i;
        goto handle_err;
      }
    }
  } // destroy handle

  // back to read mode
  if (false) { // NOLINT(readability-simplify-boolean-expr)
  handle_err:;
    RS232_Print("Error");
    switch (err.errno) {
      case MUTED: RS232_Print(": Device muted"); break;
      case BUSY: RS232_Print(": Busy bus"); break;
      case NAK_ADDRESS:
      case NAK_CONTROL:
      case NAK_MESSAGE_LENGTH:
      case NAK_DATA:
      case NAK:
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
          case NAK:
          case MUTED:
          case BUSY: __builtin_unreachable();
        }
        break;
    }
    RS232_Print("\n");
  }

  if (print.print)
    out->print(print);

  return err.errno;
}

Bus::Handle Bus::get() { return {}; };

bool Bus::Handle::sendstartbit() { return AVCLAN_sendstartbit(); };
auto Bus::Handle::readstartbit() -> Read {
  using enum Error::Read;
  auto err = AVCLAN_readstartbit();
  if (err == rSTARTBIT_TOO_LONG)
    return STARTBIT_TOO_LONG;

  if (err == rLATCHED_COMPARATOR)
    return BAD_STARTBIT;

  if (err == rSTARTBIT_TOO_SHORT)
    return STARTBIT_TOO_SHORT;

  return Read{0};
};
void Bus::Handle::send_ACK() { AVCLAN_sendbit_ACK(); };
uint8_t Bus::Handle::read_ACK() { return AVCLAN_readbit_ACK(); };

template <> inline avclan_bit_t Bus::Handle::sendbits<8>(uint8_t bits) {
  return AVCLAN_sendbyte(&bits);
};
template <> inline avclan_bit_t Bus::Handle::sendbits<1>(uint8_t bits) {
  const avclan_bit_t bit{static_cast<avclan_bit_t>(bits & 1U)};
  AVCLAN_sendbit(bit);
  return bit;
};
template <> inline avclan_bit_t Bus::Handle::readbits<8>(uint8_t *bits) {
  return static_cast<avclan_bit_t>(AVCLAN_readbyte(bits));
};

} // namespace avclan
