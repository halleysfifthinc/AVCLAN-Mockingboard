// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <span>

#include "btstack.h" // IWYU pragma: export
#include "classic/btstack_sbc_bluedroid.h"

#include "codec.hpp"

namespace avclan::bluetooth::audio {

// A2DP SBC payloads: the 1-byte media header, then whole frames. The frames
// carry their own parameters, so the config isn't needed to decode.
class SbcDecoder {
public:
  using Config = SbcConfig;

  SbcDecoder(const SbcConfig &cfg, PcmOut out);
  SbcDecoder(const SbcDecoder &) = delete;

  const SbcConfig &config() const { return cfg; }
  bool decode(std::span<const uint8_t> payload);

private:
  static void on_pcm(int16_t *data, int num_samples, int num_channels,
                     int /* sample_rate */, void *ctx);

  const SbcConfig cfg;
  const PcmOut out;
  const btstack_sbc_decoder_t *instance = nullptr;
  btstack_sbc_decoder_bluedroid_t state{};
};

static_assert(Decoder<SbcDecoder>);

} // namespace avclan::bluetooth::audio
