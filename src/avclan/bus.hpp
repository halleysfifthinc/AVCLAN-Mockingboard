// copyright (C) 2006 Marcin Slonicki <marcin@softservice.com.pl>
// copyright (C) 2007 Louis Frigon
// Copyright (C) 2015 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

/*
                                AVC LAN Theory

  The AVC LAN bus is an implementation of the IEBus (mode 1) which is a
  differential signal; IEBus is electrically (but not logically) compatible with
  CAN bus.

  - Logical `1`: Potential difference between bus lines (BUS+ pin and BUS– pin)
    is 20 mV or lower (floating).
  - Logical `0`: Potential difference between bus lines (BUS+ pin and BUS– pin)
    is 120 mV or higher (driving).

  A nominal bit length is 39 us, composed of 3 periods: preparation,
  synchronization, data.

                                Figure 1. AVCLAN Bus bit format

                             │ Prep │<─ Sync ─>│<─ Data ─>│ ...
    Driving (logical `0`)           ╭──────────╮──────────╮
                                    │          │          │
    Floating (logical `1`) ─────────╯          ╰──────────╰─────────
                             │ 6 μs │── 19 μs ─│─ 13 μs ──│

  The logical value during the data period signifies the bit value, e.g. a bit
  `0` continues the logical `0` (high potential difference between bus lines) of
  the sync period thru the data period, and a bit `1` has a logical `1`
  (low/floating potential between bus lines) during the data period. Using the
  TCB pulse-width and frequency measure mode, the total bit length differs for
  bit `1` and `0`; detailed bit timing can be found in "timing.h". The bus
  idles at low potential (floating).

  A start bit is nominally 169 us high followed by 20 us low.

  A bit `0` is dominant on the bus, which is a design choice that affects
  bit/interpretation:
    - Low addresses have priority upon transmission conflicts
    - The broadcast bit is `1` (floating, no effort) for normal communication
    - For acknowledge bits, the receiver extends the logical '0' of the sync
      period to the length of a normal bit `0`. Hence, a NAK (bit `1`) is
      literally the absence of an ACK.
*/

#pragma once

#include <concepts>
#include <cstdint>
#include <type_traits>

#include "avclan.h"
#include "avclan_phy.h" // bridge until phy has been ported
#include "frame.hpp"

namespace avclan {

struct trailer_bits_t {};
struct no_parity_t : trailer_bits_t {};   // raw bits (the broadcast bit)
struct with_parity_t : trailer_bits_t {}; // bits + parity (controller address)
struct with_ack_t : trailer_bits_t {
}; // bits + parity + ACK slot (all other fields)
inline constexpr no_parity_t no_parity{};
inline constexpr with_parity_t with_parity{};
inline constexpr with_ack_t with_ack{};

class Bus {
public:
  class Handle;
  using Error = detail::Error;

  void init();
  void mute(bool mute);
  bool is_muted() const;

  Error::Read read(uint16_t address, Frame *in, Frame::Print print);
  Error::Send send(const Frame *out, Frame::Print print);

  static Handle get();
};

class Bus::Handle {
  Handle();
  friend Bus;

public:
  ~Handle();
  Handle(const Handle &) = delete;
  Handle(Handle &&) = delete;
  using Error = detail::Error;

  bool sendstartbit();
  Error::Read readstartbit();

  template <auto N, std::unsigned_integral T,
            std::derived_from<trailer_bits_t> Trailer>
    requires(sizeof(T) < 3 && N < 16 && !std::same_as<Trailer, with_ack_t>)
  Error::Send send(T bits, Trailer /*tag*/) {
    const auto parity = sendbits<N>(bits);

    if constexpr (std::is_same_v<Trailer, with_parity_t>)
      sendbits<1>(static_cast<uint8_t>(parity));

    return Send{0};
  };

  template <auto N, std::unsigned_integral T>
    requires(sizeof(T) < 3 && N < 16)
  Error::Send send(T bits, with_ack_t /*tag*/, bool expect_ack) {
    send<N>(bits, with_parity);

    if (expect_ack && !read_ACK())
      return Send::NAK;

    return Send{0};
  };

  template <auto N, std::unsigned_integral T,
            std::derived_from<trailer_bits_t> Trailer>
    requires(sizeof(T) < 3 && N < 16 && !std::same_as<Trailer, with_ack_t>)
  Error::Read read(T *bits, Trailer /*tag*/) {
    const auto calc_parity = readbits<N>(bits);
    if constexpr (std::is_same_v<Trailer, with_parity_t>) {
      uint8_t read_parity;
      readbits<1>(&read_parity);
      if (static_cast<uint8_t>(calc_parity) != read_parity)
        return Read::BAD_PARITY;
    }
    return Read{0};
  };

  template <auto N, std::unsigned_integral T, class F>
    requires(sizeof(T) < 3 && N < 16)
  Error::Read read(T *bits, with_ack_t /*tag*/, F &&ack) {
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
  Error::Read read(T *bits, with_ack_t /*tag*/, bool ack) {
    return read<N>(bits, with_ack, [=]() { return ack; });
  }

private:
  using Read = Error::Read;
  using Send = Error::Send;
  using Bit = detail::Bit;

  static void send_ACK();
  static uint8_t read_ACK();

  template <auto N, class T> Bit sendbits(T bits);
  template <auto N, class T> Bit readbits(T *bits);

  // Temporary specializations bridging to legacy C API
  // Replace with proper (single?) template when phy has been ported
  template <auto N>
    requires(N > 1 && N < 8)
  Bit sendbits(uint8_t bits) {
    return AVCLAN_sendbitsi(&bits, N);
  };
  template <auto N>
    requires(N <= 16)
  Bit sendbits(uint16_t bits) {
    return AVCLAN_sendbitsl(&bits, N);
  };
  template <auto N>
    requires(N < 8)
  Bit readbits(uint8_t *bits) {
    return static_cast<Bit>(AVCLAN_readbitsi(bits, N));
  };
  template <auto N>
    requires(N <= 16)
  Bit readbits(uint16_t *bits) {
    return static_cast<Bit>(AVCLAN_readbitsl(bits, N));
  };
};

} // namespace avclan
