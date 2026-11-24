// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>

#include "btstack_resample.h"

namespace avclan::bluetooth::audio {

// Interleaved PCM at a variable 16.16 input/output ratio
class Resampler {
public:
  void reset(uint8_t channels) { btstack_resample_init(&state, channels); }
  uint8_t channels() const { return static_cast<uint8_t>(state.num_channels); }

  // Returns the factor applied, raised to what one chunk's output has room for
  uint32_t set_factor(uint32_t factor) {
    factor = std::max(factor, min_factor);
    btstack_resample_set_factor(&state, factor);
    return factor;
  }

  // Calls `emit(out, frames)` with each resampled chunk
  void process(const int16_t *pcm, uint16_t frames,
               std::invocable<const int16_t *, uint16_t> auto &&emit) {
    int16_t out[OUT_FRAMES * BTSTACK_RESAMPLE_MAX_CHANNELS];
    while (frames > 0) {
      const uint16_t n = std::min(frames, IN_FRAMES);
      emit(out, btstack_resample_block(&state, pcm, n, out));
      pcm += size_t{n} * channels();
      frames -= n;
    }
  }

private:
  static constexpr uint16_t IN_FRAMES = 128;
  static constexpr uint16_t OUT_FRAMES = IN_FRAMES + 16;

  btstack_resample_t state{};
  const uint32_t min_factor =
      btstack_resample_get_min_factor_for_output_capacity(IN_FRAMES,
                                                          OUT_FRAMES);
};

} // namespace avclan::bluetooth::audio
