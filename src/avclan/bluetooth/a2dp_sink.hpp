// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <optional>

#include "btstack.h" // IWYU pragma: export

#include "audio/stream.hpp"
#include "avrcp.hpp"
#include "bonds.hpp"

namespace avclan::bluetooth {

// A2DP sink: the phone's SBC stream, as transport. The codec config, media
// payloads and stream state go to `stream`. Runs on BTstack's context.
class A2dpSink {
public:
  A2dpSink(Avrcp &avrcp, audio::Stream &stream)
      : avrcp{avrcp}, stream{stream} {}

  // Before HCI_POWER_ON
  void setup();
  // AVRCP follows once A2DP signaling is up, so an absent phone is paged once
  void connect(bd_addr_t addr);

private:
  inline static constinit A2dpSink *self = nullptr;
  static void on_signaling(uint8_t packet_type, uint16_t /* channel */,
                           uint8_t *packet, uint16_t /* size */);
  static void on_media(uint8_t /* seid */, uint8_t *packet, uint16_t size);

  // Bytes 0/1 are bitmasks of sampling-freq/channel-mode and
  // block-length/subbands/allocation; 2/3 are min/max bitpool.
  static constexpr uint8_t sbc_capabilities[4] = {0xFF, 0xFF, 2, 53};
  Avrcp &avrcp;
  audio::Stream &stream;
  std::optional<BdAddr> reconnecting; // the phone our connect() pages
  uint8_t sbc_configuration[4]{};
  uint16_t cid = 0;
  uint8_t sdp_record[143]{};
};

} // namespace avclan::bluetooth
