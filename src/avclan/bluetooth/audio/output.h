// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Audio output: stereo PCM, clocked out by the port. Implemented by ports that
// build Bluetooth.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>

namespace avclan::bluetooth::audio::output {

// Left sample in the high half
using StereoFrame = uint32_t;

inline constexpr size_t BLOCK_FRAMES = 256;
inline constexpr size_t HELD_BLOCKS = 2;

using Block = std::span<const StereoFrame, BLOCK_FRAMES>;

// Claims the output's hardware for good. Before the scheduler starts.
void init();

// Clocks out blocks from `source` and returns the actual rate in Hz. `source`
// runs in the output's IRQ and returns the next block, or nullopt for silence;
// a block stays in use until HELD_BLOCKS more calls. Call start() and stop()
// from one task; the output's IRQ runs on its core.
uint32_t start(uint32_t rate,
               std::move_only_function<std::optional<Block>()> source);
void stop();

} // namespace avclan::bluetooth::audio::output
