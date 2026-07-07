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
#include "frame.hpp"

namespace avclan {

class Bus {
public:
  using Error = detail::Error;

  void init();

  bool is_active() const;
  void mute(bool mute);
  bool is_muted() const;

#ifndef NDEBUG
  void measure();
#endif

  Error::Read read(uint16_t address, Frame *in, Frame::Print print);
  Error::Send send(const Frame *out, Frame::Print print);

private:
  class Handle;
  static Handle get();
};

} // namespace avclan
