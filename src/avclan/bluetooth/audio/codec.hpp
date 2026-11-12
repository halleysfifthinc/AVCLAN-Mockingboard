// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <concepts>
#include <cstdint>
#include <span>
#include <variant>

namespace avclan::bluetooth::audio {

struct SbcConfig {
  uint32_t rate;
  uint8_t channels;
};

using CodecConfig = std::variant<SbcConfig>;

// Takes each decoded chunk: `frames` interleaved frames of `channels` samples
struct PcmOut {
  void (*fn)(void *ctx, const int16_t *pcm, uint16_t frames, uint8_t channels);
  void *ctx;
};

// Built from its config and the sink for its PCM. `decode` takes one media
// payload and is false if any of it failed to decode.
template <class D>
concept Decoder =
    std::constructible_from<D, const typename D::Config &, PcmOut> &&
    requires(D dec, const D &cdec, std::span<const uint8_t> payload) {
      { cdec.config() } -> std::same_as<const typename D::Config &>;
      { dec.decode(payload) } -> std::same_as<bool>;
    };

} // namespace avclan::bluetooth::audio
