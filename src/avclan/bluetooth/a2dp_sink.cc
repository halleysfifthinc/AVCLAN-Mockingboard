// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdint>
#include <cstdio>

#include "FreeRTOS.h" // IWYU pragma: export

#include "btstack.h" // IWYU pragma: export

#include "a2dp_sink.hpp"
#include "sdp.hpp"

namespace avclan::bluetooth {

void A2dpSink::setup() {
  configASSERT(!self);
  self = this;
  a2dp_sink_init();
  a2dp_sink_register_packet_handler(&on_signaling);
  a2dp_sink_register_media_handler(&on_media);

  avdtp_stream_endpoint_t *sep = a2dp_sink_create_stream_endpoint(
      AVDTP_AUDIO, AVDTP_CODEC_SBC, sbc_capabilities, sizeof(sbc_capabilities),
      sbc_configuration, sizeof(sbc_configuration));
  if (sep == nullptr) {
    puts("failed to create A2DP sink stream endpoint");
    return;
  }
  local_seid = avdtp_local_seid(sep);

  a2dp_sink_create_sdp_record(sdp_record, sdp_create_service_record_handle(),
                              AVDTP_SINK_FEATURE_MASK_HEADPHONE, nullptr,
                              nullptr);
  register_sdp_record(sdp_record, "A2DP sink");

  printf("A2DP sink ready (local seid %u)\n", local_seid);
}

void A2dpSink::connect(bd_addr_t addr) {
  uint16_t new_cid;
  if (const uint8_t status = a2dp_sink_establish_stream(addr, &new_cid);
      status != ERROR_CODE_SUCCESS) {
    printf("A2DP connect to %s failed to start, status 0x%02x\n",
           bd_addr_to_str(addr), status);
    return;
  }
  bd_addr_copy(reconnecting.emplace().data(), addr);
  printf("reconnecting A2DP to %s...\n", bd_addr_to_str(addr));
}

void A2dpSink::on_media([[maybe_unused]] uint8_t seid,
                        [[maybe_unused]] uint8_t *packet,
                        [[maybe_unused]] uint16_t size) {}

void A2dpSink::on_signaling(uint8_t packet_type, uint16_t /* channel */,
                            uint8_t *packet, uint16_t /* size */) {
  if (packet_type != HCI_EVENT_PACKET)
    return;
  if (hci_event_packet_get_type(packet) != HCI_EVENT_A2DP_META)
    return;

  switch (hci_event_a2dp_meta_get_subevent_code(packet)) {
    // A phone that connected on its own opens AVRCP itself
    case A2DP_SUBEVENT_SIGNALING_CONNECTION_ESTABLISHED: {
      BdAddr addr;
      a2dp_subevent_signaling_connection_established_get_bd_addr(packet,
                                                                 addr.data());
      if (addr != self->reconnecting)
        break;
      self->reconnecting.reset();
      if (a2dp_subevent_signaling_connection_established_get_status(packet) ==
          ERROR_CODE_SUCCESS)
        self->avrcp.connect(addr.data());
      break;
    }
    case A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_SBC_CONFIGURATION:
      printf(
          "SBC configured: %u Hz, %u ch\n",
          a2dp_subevent_signaling_media_codec_sbc_configuration_get_sampling_frequency(
              packet),
          a2dp_subevent_signaling_media_codec_sbc_configuration_get_num_channels(
              packet));
      break;
    case A2DP_SUBEVENT_STREAM_ESTABLISHED:
      if (const uint8_t status =
              a2dp_subevent_stream_established_get_status(packet);
          status != ERROR_CODE_SUCCESS) {
        printf("stream setup failed, status 0x%02x\n", status);
        break;
      }
      self->cid = a2dp_subevent_stream_established_get_a2dp_cid(packet);
      printf("stream established, cid 0x%04x\n", self->cid);
      break;
    case A2DP_SUBEVENT_STREAM_STARTED: puts("stream started"); break;
    case A2DP_SUBEVENT_STREAM_SUSPENDED: puts("stream suspended"); break;
    // During a takeover the old phone's release follows the new phone's setup
    case A2DP_SUBEVENT_STREAM_RELEASED:
      puts("stream released");
      if (a2dp_subevent_stream_released_get_a2dp_cid(packet) == self->cid)
        self->cid = 0;
      break;
    case A2DP_SUBEVENT_SIGNALING_CONNECTION_RELEASED:
      puts("signaling released");
      if (a2dp_subevent_signaling_connection_released_get_a2dp_cid(packet) ==
          self->cid)
        self->cid = 0;
      break;
    default: break;
  }
}

} // namespace avclan::bluetooth
