// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <variant>

namespace avclan::media {

// The model is the head unit's amplifier. Levels are on its scale, 0..MAX. The
// view is the phone's volume buttons and slider (bluetooth::Avrcp).
struct Volume {
  // The amp's loudest step (Amplifier's `attenuation` table)
  static constexpr uint8_t MAX = 47;

  // Steps that arrive before the model endpoint drains coalesce into the
  // latest level. The amp steps to it.
  struct Set {
    uint8_t level;
  };
  using Request = std::variant<Set>;

  struct Level {
    uint8_t level;
  };
  using Report = std::variant<Level>;
};

} // namespace avclan::media
