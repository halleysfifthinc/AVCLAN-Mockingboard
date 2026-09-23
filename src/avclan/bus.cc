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

  For broadcast frames the acknowledge (one) bit is sent, but an ACK response
  (zero) is not expected.
*/

#include <cstdint>
#include <cstdio>
#include <memory>

#include "FreeRTOS.h" // IWYU pragma: export

#include "avclan.h"
#include "bus.hpp"
#include "frame.hpp"
#include "hal/phy.h"
#include "stdshim.hpp"

namespace {
using Read = avclan::detail::Error::Read;
using Send = avclan::detail::Error::Send;

// The bus spec has a unit that loses arbitration retry rather than fail: "if
// the unit loses in arbitration, the frame is automatically reset up twice
// (three times in total)". Only an attempt that is outbid every time is an
// error worth reporting.
constexpr uint8_t SEND_ATTEMPTS = 3;
} // namespace

namespace avclan {

class Bus::Handle {
  // A live handle exclusively borrows the Bus for the duration of a
  // transaction; holding the reference is what forces `get()` (and thus
  // `read`/`send`) to require a mutable Bus, even though the bus hardware
  // itself is reached through free `phy_*` functions.
  Bus &bus_;
  explicit Handle(Bus &bus) : bus_{bus} { phy_guard_enter(); };
  friend Bus;

public:
  ~Handle() { phy_guard_leave(); };
  Handle(const Handle &) = delete;
  Handle(Handle &&) = delete;

  // Forward to phy API (organized so that hal/phy.h isn't public/visible at the
  // C++/library level)
  // NOLINTBEGIN(readability-convert-member-functions-to-static)
  Read read_header(bool *is_unicast) { return phy_read_header(is_unicast); };
  Read read_controller_addr(uint16_t *addr) {
    return phy_read_controller_addr(addr);
  };
  Read read_peripheral_addr(uint16_t *addr) {
    return phy_read_peripheral_addr(addr);
  };
  Read read_control(uint8_t *control) { return phy_read_control(control); };
  Read read_length(uint8_t *length) { return phy_read_length(length); };
  Read read_data(uint8_t *data, uint8_t length, uint8_t *data_index) {
    return phy_read_data(data, length, data_index);
  };

  Send send_header(bool is_unicast) { return phy_send_header(is_unicast); };
  Send send_controller_addr(uint16_t addr) {
    return phy_send_controller_addr(addr);
  };
  Send send_peripheral_addr(uint16_t addr, bool expect_ack) {
    return phy_send_peripheral_addr(addr, expect_ack);
  };
  Send send_control(uint8_t control, bool expect_ack) {
    return phy_send_control(control, expect_ack);
  };
  Send send_length(uint8_t length, bool expect_ack) {
    return phy_send_length(length, expect_ack);
  };
  Send send_data(const uint8_t *data, uint8_t length, bool expect_ack,
                 uint8_t *data_index) {
    return phy_send_data(data, length, expect_ack, data_index);
  };
  // NOLINTEND(readability-convert-member-functions-to-static)
};

void Bus::init(uint16_t address) {
  // Idempotent: the single Bus is shared by reference, so every Peripheral's
  // ctor calls init() on it — but the hardware must be brought up exactly once
  // (phy_init is not assumed re-entrant/idempotent).
  if (inited_)
    return;
  phy_init(address);
  muted_ = false; // phy_init leaves the bus TX unmuted
  inited_ = true;
};

void Bus::mute(bool mute) {
  phy_mute(mute);
  muted_ = mute; // Only update muted_ *AFTER* hardware has finished muting
};
// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void Bus::deafen(bool deaf) { phy_deafen(deaf); }

auto Bus::read(Frame::Print print)
    -> expected<std::unique_ptr<Frame>, Error::Read> {
  struct errtype {
    Read type;
    uint16_t val;
  } err = {};

  using enum Read;

  std::unique_ptr<Frame> in = Frame::acquire();
  if (!in) {
    err.type = POOL_EMPTY;
    goto handle_err;
  }

  phy_wait_frame(portMAX_DELAY);

  { // bound handle lifetime
    auto handle = get();

    err.type = handle.read_header(&in->is_unicast);
    if (err.type != Read{0})
      goto handle_err;

    err.type = handle.read_controller_addr(&in->controller_addr);
    if (err.type != Read{0}) {
      if (print.verbose)
        err.val = in->controller_addr;

      goto handle_err;
    }

    err.type = handle.read_peripheral_addr(&in->peripheral_addr);
    if (err.type != Read{0}) {
      if (print.verbose)
        err.val = in->peripheral_addr;

      goto handle_err;
    }

    err.type = handle.read_control(&in->control);
    if (err.type != Read{0}) {
      if (print.verbose)
        err.val = in->control;

      goto handle_err;
    }

    err.type = handle.read_length(&in->length);
    if (err.type != Read{0}) {
      if (print.verbose)
        err.val = in->length;

      goto handle_err;
    } else if (in->length == 0 || in->length > Frame::MAXLENGTH) {
      err.type = BAD_LENGTH_RANGE;
      err.val = in->length;
      goto handle_err;
    }

    uint8_t data_i = 0;
    err.type = handle.read_data(in->data, in->length, &data_i);
    if (err.type != Read{0}) {
      if (print.verbose)
        err.val = in->data[data_i];

      goto handle_err;
    }
  } // destroy handle

  if (false) { // NOLINT(readability-simplify-boolean-expr)
  handle_err:;
    if (err.type == NO_FRAME)
      return unexpected{NO_FRAME};
    fputs("ERR(read): ", stdout);
    switch (err.type) {
      case POOL_EMPTY: puts("failed Frame alloc"); break;
      case BAD_STARTBIT: fputs("bad start bit (other)", stdout); break;
      case STARTBIT_MISSED: fputs("missed start bit", stdout); break;
      case STARTBIT_MALFORMED:
        fputs("malformed start bit (external cause)", stdout);
        break;
      case STARTBIT_TOO_LONG: fputs("bad start bit (long)", stdout); break;
      case BAD_CONTROLLER_PARITY:
        fputs("reading controller addr.", stdout);
        goto VERBOSE;
      case BAD_PERIPHERAL_PARITY:
        fputs("reading peripheral addr.", stdout);
        goto VERBOSE;
      case BAD_CONTROL_PARITY: fputs("reading control", stdout); goto VERBOSE;
      case BAD_LENGTH_PARITY: fputs("reading length", stdout); goto VERBOSE;
      case BAD_LENGTH_RANGE: printf("bad length 0x%02X:", err.val); break;
      case BAD_DATA_PARITY: fputs("reading data", stdout); goto VERBOSE;
      case NO_FRAME:
      case BAD_PARITY:
        __builtin_unreachable();
      VERBOSE:
        if (print.verbose) {
          printf("; read 0x%02X:", err.val);
        }
    }
    putchar('\n');
  }

  // Only print if some data has been correctly received
  if (print.print && (err.type < STARTBIT_MISSED)) {
    if (err.type > BAD_DATA_PARITY)
      in->length = 0;
    in->print(print);
  }

  if (err.type != Read{0})
    return unexpected(err.type);

  return in;
}

auto Bus::send(const Frame &out, Frame::Print print) -> Send {
  struct errtype {
    // Error enum is ordered such that a lower numeric value corresponds to
    // more success
    Send type;
    uint8_t val;
  } err = {};

  using enum Send;

  if (is_muted()) {
    err.type = MUTED;
    goto handle_err;
  }

  for (uint8_t attempt = 0; attempt < SEND_ATTEMPTS; attempt++) {
    auto handle = get(); // bound handle lifetime

    err.type = Send{0};

    err.type = handle.send_header(out.is_unicast);
    if (err.type != Send{0}) // BUSY or LOST_ARBITRATION (broadcast)
      continue;

    err.type = handle.send_controller_addr(out.controller_addr);
    if (err.type != Send{0}) // LOST_ARBITRATION: other device has lower address
      continue;

    err.type = handle.send_peripheral_addr(out.peripheral_addr, out.is_unicast);
    if (err.type != Send{0})
      goto handle_err;

    err.type = handle.send_control(out.control, out.is_unicast);
    if (err.type != Send{0})
      goto handle_err;

    err.type = handle.send_length(out.length, out.is_unicast);
    if (err.type != Send{0})
      goto handle_err;

    err.type = handle.send_data(out.data, out.length, out.is_unicast, &err.val);
    if (err.type != Send{0})
      goto handle_err;

    // A phy that only queued the fields above settles them here
    uint8_t data_i = 0;
    err.type = phy_send_done(&data_i);
    if (err.type == BUSY || err.type == LOST_ARBITRATION) // Queued header
      continue;
    if (err.type != Send{0}) {
      err.val = data_i;
      goto handle_err;
    }

    break; // Sent
  }

  if (err.type != Send{0})
    goto handle_err; // Outbid (or busy) on every attempt

  // back to read mode
  if (false) { // NOLINT(readability-simplify-boolean-expr)
  handle_err:;
    fputs("Error", stdout);
    switch (err.type) {
      case MUTED: fputs(": Device muted", stdout); break;
      case BUSY: fputs(": Busy bus", stdout); break;
      case CONTENDED_BUS:
        fputs(": bus contended after arbitration", stdout);
        break;
      case LOST_ARBITRATION: fputs(": lost arbitration", stdout); break;
      case NAK_ADDRESS:
      case NAK_CONTROL:
      case NAK_MESSAGE_LENGTH:
      case NAK_DATA:
      case NAK_TOO_LONG:
      case NAK:
        fputs(" NAK: ", stdout);
        switch (err.type) {
          case NAK_ADDRESS: fputs("address", stdout); break;
          case NAK_CONTROL: fputs("Control", stdout); break;
          case NAK_MESSAGE_LENGTH: fputs("Message length", stdout); break;
          case NAK_DATA: printf(" data[%u]", err.val); break;
          case NAK_TOO_LONG: fputs("too long", stdout); break;
          case NAK:
          case MUTED:
          case CONTENDED_BUS:
          case LOST_ARBITRATION:
          case BUSY: __builtin_unreachable();
        }
        break;
    }
    putchar('\n');
  }

  if (print.print)
    out.print(print);

  return err.type;
}

Bus::Handle Bus::get() { return Handle{*this}; };

#if !defined(NDEBUG)

void Bus::set_dominant() { phy_set_dominant(); }
void Bus::set_recessive() { phy_set_recessive(); }

Send Bus::sendbyte(uint8_t byte, bool ack) {
  auto handle = get();
  uint8_t data_i = 0;
  if (const Send serr = handle.send_data(&byte, 1, ack, &data_i);
      serr != Send{0})
    return serr;
  return phy_send_done(&data_i);
}

  #ifdef MEASURE_BUS
// Debug bit-timing measurement on the one physical bus; instance-scoped like
// deafen().
// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void Bus::measure() { phy_measure(); }
  #endif
#endif

} // namespace avclan
