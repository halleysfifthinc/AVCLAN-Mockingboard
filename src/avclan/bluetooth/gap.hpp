// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

#include "btstack.h" // IWYU pragma: export

namespace avclan::bluetooth {

// Local identity, pairing policy and the pairing window. Runs on BTstack's
// context.
class Gap {
public:
  // Before HCI_POWER_ON
  void setup();

  // Discoverable, and accepting new pairings, for a limited time
  void enter_pairing();
  void exit_pairing();

private:
  btstack_timer_source_t pairing_timer{};
  bool pairing_open = false;
  uint8_t device_id_record[64]{};
};

} // namespace avclan::bluetooth
