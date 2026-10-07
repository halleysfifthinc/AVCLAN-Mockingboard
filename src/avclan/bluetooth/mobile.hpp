// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <cstdint>

#include "btstack.h" // IWYU pragma: export

#include "a2dp_sink.hpp"
#include "avrcp.hpp"
#include "bonds.hpp"
#include "gap.hpp"
#include "media/binding.hpp"
#include "media/playback.hpp"

namespace avclan::bluetooth {

// The phone's side of Mockingboard: brings up the controller and BTstack, and
// owns the Bluetooth endpoints
class Mobile {
public:
  // Bluetooth comes up once the scheduler runs
  Mobile();
  Mobile(const Mobile &) = delete;

  // Before the scheduler starts
  void attach(media::Binding<media::Playback> &b) { avrcp.attach(b); }
  void attach(media::Binding<media::Volume> &b) { avrcp.attach(b); }

#ifndef NDEBUG
  // Delete all bonds, then open the pairing window
  void forget_pairings();
  // As if the Playback view had sent it
  void inject(const media::Playback::Request &r) { avrcp.deliver(r); }
#endif

private:
  friend Bonds;

  inline static constinit Mobile *self = nullptr;
  static void on_hci_event(uint8_t packet_type, uint16_t channel,
                           uint8_t *packet, uint16_t size);

  void connect(bd_addr_t addr);
  void setup();
  void work();

  Gap gap;
  Avrcp avrcp;
  A2dpSink a2dp{avrcp};
  Bonds bonds{*this};
  btstack_packet_callback_registration_t hci_registration{
      .callback = &on_hci_event,
  };
#ifndef NDEBUG
  std::atomic<bool> forget_requested = false;
#endif
};

} // namespace avclan::bluetooth
