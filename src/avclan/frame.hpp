// copyright (C) 2006 Marcin Slonicki <marcin@softservice.com.pl>
// copyright (C) 2007 Louis Frigon
// Copyright (C) 2015 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

#include "avclan.h"

namespace avclan {
enum class Device : uint8_t;

struct Frame {
  struct Print {
    bool print : 1 = false;   // print at all
    bool binary : 1 = false;  // if printing, format as binary instead of text
    bool verbose : 1 = false; // include extra context in error reports
  };
  using Error = detail::Error;
  static constexpr int MAXLENGTH = 32;

  Error::Parse parse(const uint8_t *bytes, uint8_t len);
  void print(Print print) const;

  uint8_t reaction;
  Device owning_device;
  bool is_unicast;
  uint16_t controller_addr; // formerly "master"
  uint16_t peripheral_addr; // formerly "slave"
  uint8_t control = 0xF;
  uint8_t length;
  uint8_t data[MAXLENGTH];
};
} // namespace avclan
