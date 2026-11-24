// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <variant>

namespace avclan::media {

// Play/pause, track, position, shuffle and repeat. The model is the phone's
// media player (bluetooth::Avrcp); the view is the HU's CD-changer display and
// buttons (CDChanger).
struct Playback {
  enum class Direction : int8_t { Backward = -1, None, Forward };
  enum class RepeatMode : uint8_t { Off, Track, All };

  struct Play {};
  struct Pause {};
  // Presses of one button that arrive before the model endpoint drains merge
  // into one; a lost skip just needs another press
  struct Next {};
  struct Previous {};
  // Held until Seek{Direction::None}; a repeated or unmatched Seek{None} has
  // no effect
  struct Seek {
    Direction direction;
  };
  struct SetShuffle {
    bool on;
  };
  struct SetRepeat {
    RepeatMode mode;
  };
  using Request =
      std::variant<Play, Pause, Next, Previous, Seek, SetShuffle, SetRepeat>;

  struct Connected {
    bool connected;
  };
  struct Playing {
    bool playing;
  };
  struct Track {
    uint8_t number; // 0: none selected
  };
  struct Position {
    std::optional<std::chrono::milliseconds> position; // nullopt: no track
  };
  struct Shuffle {
    bool on;
  };
  struct Repeat {
    RepeatMode mode;
  };
  using Report =
      std::variant<Connected, Playing, Track, Position, Shuffle, Repeat>;
};

} // namespace avclan::media
