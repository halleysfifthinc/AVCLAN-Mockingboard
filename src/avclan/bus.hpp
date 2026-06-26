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

#include "avclan_defs.h"
#include "avclan_phy.h" // bridge until phy has been ported

namespace avclan {
class Bus {
public:
  struct Error {
    enum class Read : uint8_t {
      BAD_PARITY = 0x01,
      STARTBIT_TOO_SHORT = 0x80, // Start *well* above Peripher::Error::Read
                                 // (which ~inherits these values)
      STARTBIT_TOO_LONG,
      BAD_STARTBIT,
    };

    enum class Send : uint8_t {
      NAK = 0x01,
    };
  };
  class Handle;

  void init();
  void mute(bool mute);
  bool is_muted() const;
  Handle get();
};

class Bus::Handle {
  Handle() { AVCLAN_stopEvent(); }
  friend Bus;

public:
  ~Handle() { AVCLAN_startEvent(); }
  Handle(const Handle &) = delete;
  Handle(Handle &&) = delete;

  bool sendstartbit();
  Error::Read readstartbit();

  template <auto N, std::unsigned_integral T>
    requires(sizeof(T) < 3 && N < 16)
  Error::Send send(T bits, bool ack) {
    const auto parity = sendbits<N>(bits);
    sendbits<1>(static_cast<uint8_t>(parity));

    if (ack && !read_ACK())
      return Send::NAK;

    return Send{0};
  };

  template <auto N, std::unsigned_integral T, class F>
    requires(sizeof(T) < 3 && N < 16)
  Error::Read read(T *bits, F &&ack) {
    const auto calc_parity = readbits<N>(bits);
    uint8_t read_parity;
    readbits<1>(&read_parity);
    if (calc_parity != read_parity) {
      return Read::BAD_PARITY;
    } else if (ack()) {
      send_ACK();
    } else
      readbits<1>(&read_parity);

    return Read{0};
  };
  template <auto N, std::unsigned_integral T>
    requires(sizeof(T) < 3 && N < 16)
  Error::Read read(T *bits, bool ack) {
    return read<N>(bits, [=]() { return ack; });
  }

private:
  using Read = Error::Read;
  using Send = Error::Send;

  // A single bus symbol. bit_zero/bit_one carry data (and double as parity
  // values); bit_start marks a frame start bit.
  enum class avclan_bit : uint8_t {
    bit_zero = 0x00,
    bit_one = 0x01,
    bit_start = 0x10
  };

  void send_ACK();
  uint8_t read_ACK();

  template <auto N, class T> avclan_bit_t sendbits(T bits);
  template <auto N, class T> avclan_bit_t readbits(T *bits);

  // Temporary specializations bridging to legacy C API
  // Replace with proper (single?) template when phy has been ported
  template <auto N>
    requires(N > 1 && N < 8)
  avclan_bit_t sendbits(uint8_t bits) {
    return AVCLAN_sendbitsi(&bits, N);
  };
  template <auto N>
    requires(N <= 16)
  avclan_bit_t sendbits(uint16_t bits) {
    return AVCLAN_sendbitsl(&bits, N);
  };
  template <auto N>
    requires(N < 8)
  avclan_bit_t readbits(uint8_t *bits) {
    return static_cast<avclan_bit_t>(AVCLAN_readbitsi(bits, N));
  };
  template <auto N>
    requires(N <= 16)
  avclan_bit_t readbits(uint16_t *bits) {
    return static_cast<avclan_bit_t>(AVCLAN_readbitsl(bits, N));
  };
};

template <> inline avclan_bit_t Bus::Handle::sendbits<8>(uint8_t byte) {
  return AVCLAN_sendbyte(&byte);
};
template <> inline avclan_bit_t Bus::Handle::sendbits<1>(uint8_t byte) {
  const avclan_bit_t b{static_cast<avclan_bit_t>(byte & 1u)};
  AVCLAN_sendbit(b);
  return b;
};
template <> inline avclan_bit_t Bus::Handle::readbits<8>(uint8_t *byte) {
  return static_cast<avclan_bit_t>(AVCLAN_readbyte(byte));
};

} // namespace avclan
