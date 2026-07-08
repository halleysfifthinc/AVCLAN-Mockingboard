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

#include <concepts>
#include <cstdio>

#include "avclan.h"
#include "bus.hpp"
#include "frame.hpp"
#include "hal/phy.h"

namespace {
using Read = avclan::detail::Error::Read;
using Send = avclan::detail::Error::Send;
using Bit = avclan::detail::Bit;

struct trailer_bits_t {};
struct no_parity_t : trailer_bits_t {};   // raw bits (the broadcast bit)
struct with_parity_t : trailer_bits_t {}; // bits + parity (controller address)
struct with_ack_t : trailer_bits_t {
}; // bits + parity + ACK slot (all other fields)
inline constexpr no_parity_t no_parity{};
inline constexpr with_parity_t with_parity{};
inline constexpr with_ack_t with_ack{};
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

  // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
  Send sendstartbit() { return phy_send_startbit(); };
  // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
  Read readstartbit() { return phy_read_startbit(); };

  template <auto N, std::unsigned_integral T,
            std::derived_from<trailer_bits_t> Trailer>
    requires(sizeof(T) < 3 && N < 16 && !std::same_as<Trailer, with_ack_t>)
  Send send(T bits, Trailer /*tag*/) {
    const Bit parity = sendbits<N>(bits);

    if constexpr (std::is_same_v<Trailer, with_parity_t>)
      sendbits<1>(to_underlying(parity));

    return Send{0};
  };

  template <auto N, std::unsigned_integral T>
    requires(sizeof(T) < 3 && N < 16)
  Send send(T bits, with_ack_t /*tag*/, bool expect_ack) {
    send<N>(bits, with_parity);

    if (expect_ack)
      return read_ACK();

    sendbits<1>(1U); // still need to fill the ack bit slot
    return Send{0};
  };

  template <auto N, std::unsigned_integral T,
            std::derived_from<trailer_bits_t> Trailer>
    requires(sizeof(T) < 3 && N < 16 && !std::same_as<Trailer, with_ack_t>)
  Read read(T *bits, Trailer /*tag*/) {
    const Bit calc_parity = readbits<N>(bits);
    if constexpr (std::is_same_v<Trailer, with_parity_t>) {
      uint8_t read_parity;
      readbits<1>(&read_parity);
      if (to_underlying(calc_parity) != read_parity)
        return Read::BAD_PARITY;
    }
    return Read{0};
  };

  template <auto N, std::unsigned_integral T, class F>
    requires(sizeof(T) < 3 && N < 16)
  Read read(T *bits, with_ack_t /*tag*/, F &&ack) {
    if (read<N>(bits, with_parity) == Read::BAD_PARITY)
      return Read::BAD_PARITY;

    if (ack()) {
      send_ACK();
    } else {
      uint8_t slot;
      readbits<1>(&slot);
    }

    return Read{0};
  };
  template <auto N, std::unsigned_integral T>
    requires(sizeof(T) < 3 && N < 16)
  Read read(T *bits, with_ack_t /*tag*/, bool ack) {
    return read<N>(bits, with_ack, [=]() { return ack; });
  }

private:
  static void send_ACK() { phy_send_ack(); };
  static Send read_ACK() { return phy_read_ack(); };

  template <auto N, class T> Bit sendbits(T bits);
  template <auto N, class T> Bit readbits(T *bits);

  template <auto N>
    requires(N > 1 && N < 8)
  Bit sendbits(uint8_t bits) {
    return phy_send_bits_u8(&bits, N);
  };
  template <auto N>
    requires(N <= 16)
  Bit sendbits(uint16_t bits) {
    return phy_send_bits_u16(&bits, N);
  };
  template <auto N>
    requires(N < 8)
  Bit readbits(uint8_t *bits) {
    return static_cast<Bit>(phy_read_bits_u8(bits, N));
  };
  template <auto N>
    requires(N <= 16)
  Bit readbits(uint16_t *bits) {
    return static_cast<Bit>(phy_read_bits_u16(bits, N));
  };
};

template <> inline Bit Bus::Handle::sendbits<8>(uint8_t bits) {
  return phy_send_byte(&bits);
};
template <> inline Bit Bus::Handle::sendbits<1>(uint8_t bits) {
  const Bit bit{static_cast<Bit>(bits & 1U)};
  phy_send_bit(bit);
  return bit;
};
template <> inline Bit Bus::Handle::readbits<8>(uint8_t *bits) {
  return phy_read_byte(bits);
};

void Bus::init() {
  // Idempotent: the single Bus is shared by reference, so every Peripheral's
  // ctor calls init() on it — but the hardware must be brought up exactly once
  // (phy_init is not assumed re-entrant/idempotent).
  if (inited_)
    return;
  phy_init();
  muted_ = false; // phy_init leaves the bus TX unmuted
  inited_ = true;
};

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
bool Bus::is_active() const { return phy_active(); };
void Bus::mute(bool mute) {
  phy_mute(mute);
  muted_ = mute; // Only update muted_ *AFTER* hardware has finished muting
};

auto Bus::read(uint16_t address, Frame *in, Frame::Print print) -> Read {
  struct errtype {
    Read errno;
    uint16_t val;
  } err = {};

  using enum Read;

  { // bound handle lifetime
    auto handle = get();

    bool shouldACK = false;
    uint8_t tmp = 0;

    err.errno = handle.readstartbit();
    if (err.errno != Read{0})
      goto handle_err;

    handle.read<1>(&tmp, no_parity);
    in->is_unicast = (tmp != 0U);

    if (auto rerr = handle.read<12>(&in->controller_addr, with_parity);
        rerr == BAD_PARITY) {
      err.errno = BAD_CONTROLLER_PARITY;
      if (print.verbose)
        err.val = in->controller_addr;

      goto handle_err;
    }

    // Using lambda for delayed evaluation of peripheral_addr field
    // deref, which will be written by the time the lambda is evaluated
    auto should_ack_lambda = [&]() {
      shouldACK = !is_muted() && (in->peripheral_addr == address);
      return shouldACK;
    };
    if (auto rerr =
            handle.read<12>(&in->peripheral_addr, with_ack, should_ack_lambda);
        rerr == BAD_PARITY) {
      err.errno = BAD_PERIPHERAL_PARITY;
      if (print.verbose)
        err.val = in->peripheral_addr;

      goto handle_err;
    }

    if (auto rerr = handle.read<4>(&in->control, with_ack, shouldACK);
        rerr == BAD_PARITY) {
      err.errno = BAD_CONTROL_PARITY;
      if (print.verbose)
        err.val = in->control;

      goto handle_err;
    }

    if (auto rerr = handle.read<8>(&in->length, with_ack, shouldACK);
        rerr == BAD_PARITY) {
      err.errno = BAD_LENGTH_PARITY;
      if (print.verbose)
        err.val = in->length;

      goto handle_err;
    }

    if (in->length == 0 || in->length > Frame::MAXLENGTH) {
      err.errno = BAD_LENGTH_RANGE;
      err.val = in->length;
      goto handle_err;
    }

    for (uint8_t i = 0; i < in->length; i++) {
      if (auto rerr = handle.read<8>(&in->data[i], with_ack, shouldACK);
          rerr == BAD_PARITY) {
        err.errno = BAD_DATA_PARITY;
        if (print.verbose)
          err.val = in->data[i];

        goto handle_err;
      }
    }
  } // destroy handle

  if (false) { // NOLINT(readability-simplify-boolean-expr)
  handle_err:;
    fputs("ERR(read): ", stdout);
    switch (err.errno) {
      case BAD_STARTBIT: fputs("bad start bit (other)", stdout); break;
      case STARTBIT_TOO_SHORT: fputs("bad start bit (short)", stdout); break;
      case STARTBIT_TOO_LONG: fputs("bad start bit (long)", stdout); break;
      case BAD_CONTROLLER_PARITY:
        fputs("reading controller addr.", stdout);
        goto VERBOSE;
      case BAD_PERIPHERAL_PARITY:
        fputs("reading peripheral addr.", stdout);
        goto VERBOSE;
      case BAD_CONTROL_PARITY: fputs("reading control", stdout); goto VERBOSE;
      case BAD_LENGTH_PARITY: fputs("reading length", stdout); goto VERBOSE;
      case BAD_LENGTH_RANGE: printf("bad length 0x%02X", err.val); break;
      case BAD_DATA_PARITY: fputs("reading data", stdout); goto VERBOSE;
      case BAD_PARITY:
        __builtin_unreachable();
      VERBOSE:
        if (print.verbose) {
          printf("; read 0x%02X", err.val);
        }
    }
    putchar('\n');
  }

  // Only print if some data has been correctly received
  if (print.print && (err.errno < STARTBIT_TOO_SHORT)) {
    if (err.errno > BAD_DATA_PARITY)
      in->length = 0;
    in->print(print);
  }

  return err.errno;
}

auto Bus::send(const Frame *out, Frame::Print print) -> Send {
  struct errtype {
    // Error enum is ordered such that a lower numeric value corresponds to
    // more success
    Send errno;
    uint8_t val;
  } err = {};

  using enum Send;

  if (is_muted()) {
    err.errno = MUTED;
    goto handle_err;
  }

  { // bound handle lifetime
    auto handle = get();

    if (handle.sendstartbit() == BUSY) {
      // Some other device is already driving the bus
      err.errno = BUSY;
      goto handle_err;
    }

    handle.send<1>(static_cast<uint8_t>(out->is_unicast), no_parity);

    handle.send<12>(out->controller_addr, with_parity);

    if (auto serr =
            handle.send<12>(out->peripheral_addr, with_ack, out->is_unicast);
        serr == NAK) {
      err.errno = NAK_ADDRESS;
      goto handle_err;
    }

    if (auto serr = handle.send<4>(out->control, with_ack, out->is_unicast);
        serr == NAK) {
      err.errno = NAK_CONTROL;
      goto handle_err;
    }

    if (auto serr = handle.send<8>(out->length, with_ack, out->is_unicast);
        serr == NAK) {
      err.errno = NAK_MESSAGE_LENGTH;
      goto handle_err;
    }

    for (uint8_t i = 0; i < out->length; i++) {
      // Based on the µPD6708 datasheet, ACK bit for broadcast doesn't seem
      // necessary (i.e. This deviates from the previous broadcast specific
      // function that sent an extra `1` bit after each byte/parity)
      // Explanation for why audio-group broadcast state report isn't working?
      if (auto serr = handle.send<8>(out->data[i], with_ack, out->is_unicast);
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
    fputs("Error", stdout);
    switch (err.errno) {
      case MUTED: fputs(": Device muted", stdout); break;
      case BUSY: fputs(": Busy bus", stdout); break;
      case NAK_ADDRESS:
      case NAK_CONTROL:
      case NAK_MESSAGE_LENGTH:
      case NAK_DATA:
      case NAK:
        fputs(" NAK: ", stdout);
        switch (err.errno) {
          case NAK_ADDRESS: fputs("address", stdout); break;
          case NAK_CONTROL: fputs("Control", stdout); break;
          case NAK_MESSAGE_LENGTH: fputs("Message length", stdout); break;
          case NAK_DATA: printf(" data[%u]", err.val); break;
          case NAK:
          case MUTED:
          case BUSY: __builtin_unreachable();
        }
        break;
    }
    putchar('\n');
  }

  if (print.print)
    out->print(print);

  return err.errno;
}

Bus::Handle Bus::get() { return Handle{*this}; };

#ifndef NDEBUG
// Debug bit-timing measurement on the one physical bus; instance-scoped for the
// same reason as is_active().
// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void Bus::measure() { phy_measure(); }
#endif

} // namespace avclan
