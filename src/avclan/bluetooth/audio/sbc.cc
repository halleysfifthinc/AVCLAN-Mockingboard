// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdint>
#include <span>

#include "sbc.hpp"

namespace avclan::bluetooth::audio {

SbcDecoder::SbcDecoder(const SbcConfig &cfg, PcmOut out) : cfg{cfg}, out{out} {
  instance = btstack_sbc_decoder_bluedroid_init_instance(&state);
  instance->configure(&state, SBC_MODE_STANDARD, &on_pcm, this);
}

bool SbcDecoder::decode(std::span<const uint8_t> payload) {
  // Fragmented frames (header bit 7) are never reassembled
  if (payload.size() < 2 || (payload[0] & 0x80) != 0)
    return false;
  const int bad = state.bad_frames_nr;
  instance->decode_signed_16(&state, 0, payload.data() + 1,
                             static_cast<uint16_t>(payload.size() - 1));
  return state.bad_frames_nr == bad;
}

void SbcDecoder::on_pcm(int16_t *data, int num_samples, int num_channels,
                        int /* sample_rate */, void *ctx) {
  const PcmOut &out = static_cast<SbcDecoder *>(ctx)->out;
  out.fn(out.ctx, data, static_cast<uint16_t>(num_samples),
         static_cast<uint8_t>(num_channels));
}

} // namespace avclan::bluetooth::audio
