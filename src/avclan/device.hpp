// copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <concepts>
#include <cstdint>

#include "avclan.h"
#include "frame.hpp"

namespace avclan {

enum class Device : uint8_t {
  LAN = 0x00,
  COMM_CTRL = 0x01,
  COMM_v1 = 0x11,
  COMM_v2 = 0x12,
  SW = 0x21,
  SW_NAME = 0x23,
  SW_CONVERTING = 0x24,
  CMD_SW = 0x25,
  STATUS = 0x31,
  BEEP_HU = 0x28,
  BEEP_SPEAKERS = 0x29,
  TUNER = 0x60,
  TAPE_DECK = 0x61,
  CD = 0x62,
  CD_CHANGER = 0x63,
  AUDIO_AMP = 0x74,
};

template <class T>
concept DeviceInterface =
    requires { std::integral_constant<Device, T::id>{}; } &&
    requires(T dev, const Frame *in, Frame *out, detail::Error::Send err,
             uint16_t peripheral) {
      dev.init();
      dev.handle(in, out);
      dev.enable(out);
      dev.react(out, err);
      { dev.pending() } -> std::convertible_to<bool>;
      dev.resolvepending();
      dev.emit(out, peripheral);
    };
} // namespace avclan
