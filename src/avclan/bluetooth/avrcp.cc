// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <concepts>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <optional>
#include <variant>

#include "btstack.h" // IWYU pragma: export

#include "avrcp.hpp"
#include "platform.h"
#include "sdp.hpp"

namespace avclan::bluetooth {
namespace {
using media::Playback;
using media::Volume;

bool is_playing(avrcp_playback_status_t status) {
  switch (status) {
    case AVRCP_PLAYBACK_STATUS_PLAYING:
    case AVRCP_PLAYBACK_STATUS_FWD_SEEK:
    case AVRCP_PLAYBACK_STATUS_REV_SEEK: return true;
    case AVRCP_PLAYBACK_STATUS_STOPPED:
    case AVRCP_PLAYBACK_STATUS_PAUSED:
    case AVRCP_PLAYBACK_STATUS_ERROR:
    default: return false;
  }
}

// The query response and the PLAYER_APPLICATION_SETTING_CHANGED value_ids share
// these values
Playback::RepeatMode to_repeat(avrcp_repeat_mode_t mode) {
  using enum Playback::RepeatMode;
  switch (mode) {
    case AVRCP_REPEAT_MODE_SINGLE_TRACK: return Track;
    case AVRCP_REPEAT_MODE_ALL_TRACKS:
    case AVRCP_REPEAT_MODE_GROUP: return All;
    case AVRCP_REPEAT_MODE_INVALID:
    case AVRCP_REPEAT_MODE_OFF:
    case AVRCP_REPEAT_MODE_RFU:
    default: return Off;
  }
}

bool is_shuffled(avrcp_shuffle_mode_t mode) {
  switch (mode) {
    case AVRCP_SHUFFLE_MODE_ALL_TRACKS:
    case AVRCP_SHUFFLE_MODE_GROUP: return true;
    case AVRCP_SHUFFLE_MODE_INVALID:
    case AVRCP_SHUFFLE_MODE_OFF:
    case AVRCP_SHUFFLE_MODE_RFU:
    default: return false;
  }
}

void restart(btstack_timer_source_t &timer, uint32_t ms) {
  btstack_run_loop_remove_timer(&timer);
  btstack_run_loop_set_timer(&timer, ms);
  btstack_run_loop_add_timer(&timer);
}
} // namespace

void Avrcp::setup() {
  configASSERT(!self);
  self = this;
  avrcp_init();
  avrcp_register_packet_handler(&on_connection);
  avrcp_controller_init();
  avrcp_controller_register_packet_handler(&on_controller);
  avrcp_target_init();
  avrcp_target_register_packet_handler(&on_target);

  // A phone looks for the controller and target records separately
  avrcp_controller_create_sdp_record(
      controller_record, sdp_create_service_record_handle(),
      1 << AVRCP_CONTROLLER_SUPPORTED_FEATURE_CATEGORY_PLAYER_OR_RECORDER,
      nullptr, nullptr);
  register_sdp_record(controller_record, "AVRCP controller");

  avrcp_target_create_sdp_record(
      target_record, sdp_create_service_record_handle(),
      1 << AVRCP_TARGET_SUPPORTED_FEATURE_CATEGORY_MONITOR_OR_AMPLIFIER,
      nullptr, nullptr);
  register_sdp_record(target_record, "AVRCP target");

  puts("AVRCP controller + target ready");
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void Avrcp::connect(bd_addr_t addr) {
  if (cid != 0)
    return; // the phone connected first
  uint16_t new_cid;
  if (const uint8_t status = avrcp_connect(addr, &new_cid);
      status != ERROR_CODE_SUCCESS) {
    printf("AVRCP connect to %s failed to start, status 0x%02x\n",
           bd_addr_to_str(addr), status);
    return;
  }
  printf("reconnecting AVRCP to %s...\n", bd_addr_to_str(addr));
}

void Avrcp::drain() {
  media::Model<Avrcp, Playback>::drain();
  media::View<Avrcp, Volume>::drain();
}

void Avrcp::wake(const Playback::Request & /* msg */) { platform::wake(); }

void Avrcp::wake(const Volume::Report & /* msg */) { platform::wake(); }

// The head unit owns the volume; the phone's slider only mirrors it
void Avrcp::receive(const Volume::Level &lvl) {
  const uint8_t level = std::min(lvl.level, Volume::MAX);
  volume =
      static_cast<uint8_t>(((level * 127) + (Volume::MAX / 2)) / Volume::MAX);
  if (cid != 0)
    avrcp_target_volume_changed(cid, volume);
}

void Avrcp::receive(const Playback::Play &p) { enqueue(p); }

void Avrcp::receive(const Playback::Pause &p) { enqueue(p); }

void Avrcp::receive(const Playback::Next &n) { enqueue(n); }

void Avrcp::receive(const Playback::Previous &p) { enqueue(p); }

// A press already sent is released; a press still queued is replaced
void Avrcp::receive(const Playback::Seek &seek) {
  using enum Playback::Direction;
  if (held != None) {
    if (seek.direction == held)
      return;
    if (const uint8_t status = avrcp_controller_release_press_and_hold_cmd(cid);
        status != ERROR_CODE_SUCCESS)
      printf("AVRCP release failed, status 0x%02x\n", status);
    held = None;
    if (seek.direction == None)
      return;
  }
  enqueue(seek);
}

void Avrcp::receive(const Playback::SetShuffle &s) { enqueue(s); }

void Avrcp::receive(const Playback::SetRepeat &r) { enqueue(r); }

void Avrcp::enqueue(const Command &cmd) {
  if (cid == 0) {
    printf("AVRCP command %u dropped: not connected\n",
           static_cast<unsigned>(cmd.index()));
    return;
  }
  pending.push(cmd);
  // Commands go out from a timer, not from inside BTstack's event callbacks,
  // which may still be parsing the response that raised the event
  restart(pump_timer, 0);
}

void Avrcp::pump() {
  // Retry interval while the controller is busy with another command
  constexpr uint32_t RETRY_MS = 50;
  // Previous may restart the track instead, which raises no TRACK_CHANGED
  constexpr uint32_t TRACK_CHECK_MS = 1000;
  // Outlasts BTstack's 250 ms response timeout, for a response that raises no
  // event
  constexpr uint32_t STALL_MS = 300;

  while (const auto cmd = pending.pop()) {
    const uint8_t status = std::visit(
        [this]<class C>(const C &c) -> uint8_t {
          if constexpr (std::same_as<C, Playback::Play>) {
            return avrcp_controller_play(cid);
          } else if constexpr (std::same_as<C, Playback::Pause>) {
            return avrcp_controller_pause(cid);
          } else if constexpr (std::same_as<C, Playback::Next>) {
            return avrcp_controller_forward(cid);
          } else if constexpr (std::same_as<C, Playback::Previous>) {
            const uint8_t result = avrcp_controller_backward(cid);
            if (result == ERROR_CODE_SUCCESS)
              restart(track_check_timer, TRACK_CHECK_MS);
            return result;
          } else if constexpr (std::same_as<C, Playback::Seek>) {
            uint8_t result = ERROR_CODE_SUCCESS;
            switch (c.direction) {
              case Playback::Direction::Forward:
                result = avrcp_controller_press_and_hold_fast_forward(cid);
                break;
              case Playback::Direction::Backward:
                result = avrcp_controller_press_and_hold_rewind(cid);
                break;
              case Playback::Direction::None: break; // released before sent
            }
            if (result == ERROR_CODE_SUCCESS)
              held = c.direction;
            return result;
          } else if constexpr (std::same_as<C, Playback::SetShuffle>) {
            return avrcp_controller_set_shuffle_mode(
                cid,
                c.on ? AVRCP_SHUFFLE_MODE_ALL_TRACKS : AVRCP_SHUFFLE_MODE_OFF);
          } else if constexpr (std::same_as<C, Playback::SetRepeat>) {
            avrcp_repeat_mode_t mode = AVRCP_REPEAT_MODE_OFF;
            switch (c.mode) {
              case Playback::RepeatMode::Off: break;
              case Playback::RepeatMode::Track:
                mode = AVRCP_REPEAT_MODE_SINGLE_TRACK;
                break;
              case Playback::RepeatMode::All:
                mode = AVRCP_REPEAT_MODE_ALL_TRACKS;
                break;
            }
            return avrcp_controller_set_repeat_mode(cid, mode);
          } else if constexpr (std::same_as<C, NowPlaying>) {
            return avrcp_controller_get_now_playing_info(cid);
          } else if constexpr (std::same_as<C, PlayStatus>) {
            return avrcp_controller_get_play_status(cid);
          } else if constexpr (std::same_as<C, ShuffleAndRepeat>) {
            return avrcp_controller_query_shuffle_and_repeat_modes(cid);
          } else {
            static_assert(false, "unhandled AVRCP command");
          }
        },
        *cmd);
    if (status == ERROR_CODE_COMMAND_DISALLOWED) {
      pending.push_front(*cmd);
      // A held seek's release raises OPERATION_COMPLETE, which re-pumps
      if (held == Playback::Direction::None)
        restart(pump_timer, RETRY_MS);
      return;
    }
    if (status == ERROR_CODE_SUCCESS) {
      // The response's event normally re-pumps first
      restart(pump_timer, STALL_MS);
      return;
    }
    printf("AVRCP command %u failed, status 0x%02x\n",
           static_cast<unsigned>(cmd->index()), status);
  }
}

void Avrcp::on_connection(uint8_t packet_type, uint16_t /* channel */,
                          uint8_t *packet, uint16_t /* size */) {
  if (packet_type != HCI_EVENT_PACKET)
    return;
  if (hci_event_packet_get_type(packet) != HCI_EVENT_AVRCP_META)
    return;

  switch (hci_event_avrcp_meta_get_subevent_code(packet)) {
    case AVRCP_SUBEVENT_CONNECTION_ESTABLISHED: {
      if (avrcp_subevent_connection_established_get_status(packet) !=
          ERROR_CODE_SUCCESS)
        break;
      self->cid = avrcp_subevent_connection_established_get_avrcp_cid(packet);
      printf("AVRCP connected, cid 0x%04x\n", self->cid);
      // A phone taking over doesn't inherit the old phone's hold or commands
      self->held = Playback::Direction::None;
      self->pending = {};
      self->send(Playback::Connected{true});

      // Notifications stay enabled for the connection. The phone's app owns
      // repeat/shuffle and can change them itself, and a player switch may
      // change them without a settings notification.
      for (const auto event : {
               AVRCP_NOTIFICATION_EVENT_PLAYBACK_STATUS_CHANGED,
               AVRCP_NOTIFICATION_EVENT_TRACK_CHANGED,
               AVRCP_NOTIFICATION_EVENT_PLAYBACK_POS_CHANGED,
               AVRCP_NOTIFICATION_EVENT_PLAYER_APPLICATION_SETTING_CHANGED,
               AVRCP_NOTIFICATION_EVENT_ADDRESSED_PLAYER_CHANGED,
           })
        avrcp_controller_enable_notification(self->cid, event);
      self->enqueue(NowPlaying{});
      self->enqueue(PlayStatus{});
      self->enqueue(ShuffleAndRepeat{});

      // A phone registers only for the events the target lists
      avrcp_target_support_event(self->cid,
                                 AVRCP_NOTIFICATION_EVENT_VOLUME_CHANGED);
      // So the phone's volume slider shows our volume
      avrcp_target_volume_changed(self->cid, self->volume);
      break;
    }
    case AVRCP_SUBEVENT_CONNECTION_RELEASED:
      // After a takeover, the old phone's release follows the new connection
      if (avrcp_subevent_connection_released_get_avrcp_cid(packet) != self->cid)
        break;
      puts("AVRCP disconnected");
      self->cid = 0;
      self->held = Playback::Direction::None;
      self->pending = {};
      btstack_run_loop_remove_timer(&self->pump_timer);
      btstack_run_loop_remove_timer(&self->track_check_timer);
      self->send(Playback::Connected{false});
      break;
    default: break;
  }
}

void Avrcp::on_controller(uint8_t packet_type, uint16_t /* channel */,
                          uint8_t *packet, uint16_t /* size */) {
  if (packet_type != HCI_EVENT_PACKET)
    return;
  if (hci_event_packet_get_type(packet) != HCI_EVENT_AVRCP_META)
    return;

  switch (hci_event_avrcp_meta_get_subevent_code(packet)) {
    case AVRCP_SUBEVENT_SHUFFLE_AND_REPEAT_MODE: {
      const auto repeat = static_cast<avrcp_repeat_mode_t>(
          avrcp_subevent_shuffle_and_repeat_mode_get_repeat_mode(packet));
      const auto shuffle = static_cast<avrcp_shuffle_mode_t>(
          avrcp_subevent_shuffle_and_repeat_mode_get_shuffle_mode(packet));
      self->send(Playback::Repeat{to_repeat(repeat)});
      self->send(Playback::Shuffle{is_shuffled(shuffle)});
      printf("shuffle/repeat state (queried): repeat=%d shuffle=%d\n", repeat,
             shuffle);
      break;
    }
    case AVRCP_SUBEVENT_NOTIFICATION_EVENT_PLAYER_APPLICATION_SETTING_CHANGED: {
      uint8_t attribute_id =
          avrcp_subevent_notification_event_player_application_setting_changed_get_attribute_id(
              packet);
      uint8_t value_id =
          avrcp_subevent_notification_event_player_application_setting_changed_get_value_id(
              packet);
      switch (attribute_id) {
        case AVRCP_PLAYER_APPLICATION_SETTING_ATTRIBUTE_ID_REPEAT_MODE_STATUS:
          self->send(Playback::Repeat{
              to_repeat(static_cast<avrcp_repeat_mode_t>(value_id)),
          });
          printf("phone changed repeat mode -> %u\n", value_id);
          break;
        case AVRCP_PLAYER_APPLICATION_SETTING_ATTRIBUTE_ID_SHUFFLE_STATUS:
          self->send(Playback::Shuffle{
              is_shuffled(static_cast<avrcp_shuffle_mode_t>(value_id)),
          });
          printf("phone changed shuffle mode -> %u\n", value_id);
          break;
        default: break; // equalizer/scan: not tracked
      }
      break;
    }
    // Accepted or not, the head unit learns the outcome from the query's report
    case AVRCP_SUBEVENT_PLAYER_APPLICATION_VALUE_RESPONSE:
      self->enqueue(ShuffleAndRepeat{});
      break;
    case AVRCP_SUBEVENT_NOTIFICATION_ADDRESSED_PLAYER_CHANGED:
      printf(
          "addressed player changed -> id 0x%04x; re-syncing shuffle/repeat\n",
          avrcp_subevent_notification_addressed_player_changed_get_player_id(
              packet));
      self->enqueue(ShuffleAndRepeat{});
      break;
    case AVRCP_SUBEVENT_NOTIFICATION_PLAYBACK_STATUS_CHANGED:
      self->send(Playback::Playing{
          is_playing(static_cast<avrcp_playback_status_t>(
              avrcp_subevent_notification_playback_status_changed_get_play_status(
                  packet))),
      });
      break;
    case AVRCP_SUBEVENT_PLAY_STATUS:
      // Here 0xFFFFFFFF means the player doesn't report position, not "no
      // track"
      if (const uint32_t ms =
              avrcp_subevent_play_status_get_song_position(packet);
          ms != 0xFFFFFFFF)
        self->send(Playback::Position{std::chrono::milliseconds{ms}});
      self->send(Playback::Playing{
          is_playing(static_cast<avrcp_playback_status_t>(
              avrcp_subevent_play_status_get_play_status(packet))),
      });
      break;
    case AVRCP_SUBEVENT_NOTIFICATION_TRACK_CHANGED:
      btstack_run_loop_remove_timer(&self->track_check_timer);
      self->enqueue(NowPlaying{});
      self->enqueue(PlayStatus{});
      break;
    case AVRCP_SUBEVENT_NOTIFICATION_PLAYBACK_POS_CHANGED: {
      const uint32_t ms =
          avrcp_subevent_notification_playback_pos_changed_get_playback_position_ms(
              packet);
      self->send(Playback::Position{
          ms == AVRCP_NO_TRACK_SELECTED_PLAYBACK_POSITION_CHANGED
              ? std::nullopt
              : std::optional{std::chrono::milliseconds{ms}},
      });
      break;
    }
    case AVRCP_SUBEVENT_NOW_PLAYING_TRACK_INFO:
      self->send(Playback::Track{
          avrcp_subevent_now_playing_track_info_get_track(packet),
      });
      break;
    case AVRCP_SUBEVENT_NOW_PLAYING_TITLE_INFO:
      printf("title: %.*s\n",
             avrcp_subevent_now_playing_title_info_get_value_len(packet),
             (const char *)avrcp_subevent_now_playing_title_info_get_value(
                 packet));
      break;
    case AVRCP_SUBEVENT_NOW_PLAYING_ARTIST_INFO:
      printf("artist: %.*s\n",
             avrcp_subevent_now_playing_artist_info_get_value_len(packet),
             (const char *)avrcp_subevent_now_playing_artist_info_get_value(
                 packet));
      break;
    case AVRCP_SUBEVENT_NOW_PLAYING_SONG_LENGTH_MS_INFO:
      printf("song length: %" PRIu32 " ms\n",
             avrcp_subevent_now_playing_song_length_ms_info_get_song_length(
                 packet));
      break;
    default: break;
  }

  // The response that raised this event may have freed the controller
  restart(self->pump_timer, 0);
}

void Avrcp::on_target(uint8_t packet_type, uint16_t /* channel */,
                      uint8_t *packet, uint16_t /* size */) {
  if (packet_type != HCI_EVENT_PACKET)
    return;
  if (hci_event_packet_get_type(packet) != HCI_EVENT_AVRCP_META)
    return;

  switch (hci_event_avrcp_meta_get_subevent_code(packet)) {
    case AVRCP_SUBEVENT_NOTIFICATION_VOLUME_CHANGED: {
      auto absolute =
          avrcp_subevent_notification_volume_changed_get_absolute_volume(
              packet);
      self->volume = absolute;
#ifndef NDEBUG
      printf("phone set absolute volume -> %u/127\n", absolute);
#endif
      self->send(Volume::Set{
          .level = static_cast<uint8_t>(((absolute * Volume::MAX) + (127 / 2)) /
                                        127),
      });
      break;
    }
    default: break;
  }
}

} // namespace avclan::bluetooth
