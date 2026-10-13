// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <variant>

#include "btstack.h" // IWYU pragma: export

#include "media/binding.hpp"
#include "media/playback.hpp"
#include "media/volume.hpp"

namespace avclan::bluetooth {

class Mobile;

// AVRCP controller (playback) and target (absolute volume). Except for
// deliver(), runs on BTstack's context.
class Avrcp : public media::Model<Avrcp, media::Playback>,
              public media::View<Avrcp, media::Volume> {
  using Playback = media::Playback;
  using Volume = media::Volume;

public:
  using media::Model<Avrcp, Playback>::attach;
  using media::View<Avrcp, Volume>::attach;
  using media::Model<Avrcp, Playback>::deliver;
  using media::View<Avrcp, Volume>::deliver;

  // Before HCI_POWER_ON
  void setup();
  void connect(bd_addr_t addr);
  void drain();

  void receive(const Volume::Level &l);

  void receive(const Playback::Play &);
  void receive(const Playback::Pause &);
  void receive(const Playback::Next &);
  void receive(const Playback::Previous &);
  void receive(const Playback::Seek &s);
  void receive(const Playback::SetShuffle &s);
  void receive(const Playback::SetRepeat &r);

private:
  friend Mobile;
  friend media::Model<Avrcp, Playback>;
  friend media::View<Avrcp, Volume>;

  Avrcp() = default;

  struct NowPlaying {};
  struct PlayStatus {};
  struct ShuffleAndRepeat {};
  // The controller takes one command at a time; the rest wait in `pending`
  using Command = std::variant<Playback::Play, Playback::Pause, Playback::Next,
                               Playback::Previous, Playback::Seek,
                               Playback::SetShuffle, Playback::SetRepeat,
                               NowPlaying, PlayStatus, ShuffleAndRepeat>;

  inline static constinit Avrcp *self = nullptr;
  static void on_connection(uint8_t packet_type, uint16_t /* channel */,
                            uint8_t *packet, uint16_t /* size */);
  static void on_controller(uint8_t packet_type, uint16_t /* channel */,
                            uint8_t *packet, uint16_t /* size */);
  static void on_target(uint8_t packet_type, uint16_t /* channel */,
                        uint8_t *packet, uint16_t /* size */);

  using media::Model<Avrcp, Playback>::send;
  using media::View<Avrcp, Volume>::send;

  void wake(const Playback::Request &msg);
  void wake(const Volume::Report &msg);

  void pump();
  void enqueue(const Command &cmd);

  uint16_t cid = 0; // 0: not connected
  Playback::Direction held = Playback::Direction::None;
  media::Mailbox<Command> pending;
  btstack_timer_source_t pump_timer{
      .process =
          [](btstack_timer_source_t *ts) static {
            static_cast<Avrcp *>(btstack_run_loop_get_timer_context(ts))
                ->pump();
          },
      .context = this,
  };
  btstack_timer_source_t track_check_timer{
      .process =
          [](btstack_timer_source_t *ts) static {
            static_cast<Avrcp *>(btstack_run_loop_get_timer_context(ts))
                ->enqueue(NowPlaying{});
          },
      .context = this,
  };
  uint8_t controller_record[114]{};
  uint8_t target_record[107]{};
};

} // namespace avclan::bluetooth
