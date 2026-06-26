// copyright (C) 2006 Marcin Slonicki <marcin@softservice.com.pl>
// copyright (C) 2007 Louis Frigon
// Copyright (C) 2015 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "bus.hpp"
#include "avclan_defs.h"
#include "avclan_phy.h" // bridge until phy has been ported

namespace avclan {
void Bus::init() { AVCLAN_busInit(); };
void Bus::mute(bool mute) { AVCLAN_muteDevice(mute); };
bool Bus::is_muted() const { return AVCLAN_ismuted(); };
Bus::Handle Bus::get() { return {}; };

bool Bus::Handle::sendstartbit() { return AVCLAN_sendstartbit(); };
Bus::Error::Read Bus::Handle::readstartbit() {
  using enum Error::Read;
  auto err = AVCLAN_readstartbit();
  if (err == rSTARTBIT_TOO_LONG)
    return STARTBIT_TOO_LONG;
  else if (err == rLATCHED_COMPARATOR)
    return BAD_STARTBIT;
  else if (err == rSTARTBIT_TOO_SHORT)
    return STARTBIT_TOO_SHORT;
  else
    return Read{0};
};
void Bus::Handle::send_ACK() { AVCLAN_sendbit_ACK(); };
uint8_t Bus::Handle::read_ACK() { return AVCLAN_readbit_ACK(); };

} // namespace avclan
