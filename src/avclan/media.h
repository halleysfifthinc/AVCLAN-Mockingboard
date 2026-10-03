// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// The audio source: devices emulate head-unit button presses to it. The app
// provides the source (see Bluetooth).

#pragma once

#include <cstdint>

#include "avclan.h"

namespace avclan {

// Media functions a source device can perform (ultimately bounded by the Action
// set above).
enum class MediaAction : uint8_t {
  Play,
  Pause,
  Skip_Forward,
  Skip_Forward_Release,
  Skip_Backward,
  Skip_Backward_Release,
  Track_Next,
  Track_Prev,
  Repeat_Single_On,
  Repeat_Single_Off,
  Repeat_All_On,
  Repeat_All_Off,
  Shuffle_On,
  Shuffle_Off,
  Volume_Up,
  Volume_Down,
};

// Emulate a media function (button press) on the source device. Never blocks;
// actions run in order on the source's context.
inline void media_action(MediaAction fn) {};

} // namespace avclan
