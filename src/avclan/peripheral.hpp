// copyright (C) 2006 Marcin Slonicki <marcin@softservice.com.pl>
// copyright (C) 2007 Louis Frigon
// Copyright (C) 2015 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "avclan_defs.h"
#include "bus.hpp"

#include <cstdint>

namespace avclan {
class Peripheral {
public:
  struct Error {
    // Error enums are ordered such that a lower numeric value corresponds to
    // more progress/success before an error occured, with 0 being no errors
    enum class Read : uint8_t {
      BAD_DATA_PARITY = 0x01,
      BAD_LENGTH_RANGE,
      BAD_LENGTH_PARITY,
      BAD_PERIPHERAL_PARITY,
      BAD_CONTROLLER_PARITY,
      BAD_CONTROL_PARITY,
      STARTBIT_TOO_SHORT =
          static_cast<uint8_t>(Bus::Error::Read::STARTBIT_TOO_SHORT),
      STARTBIT_TOO_LONG =
          static_cast<uint8_t>(Bus::Error::Read::STARTBIT_TOO_LONG),
      BAD_STARTBIT = static_cast<uint8_t>(Bus::Error::Read::BAD_STARTBIT),
    };

    enum class Send : uint8_t {
      NAK_DATA = 0x01,
      NAK_MESSAGE_LENGTH,
      NAK_CONTROL,
      NAK_ADDRESS,
      BUSY,
      MUTED,
    };
  };

  Peripheral(Bus bus, uint16_t address) : bus{bus}, address{address} {
    bus.init();
  }

  Error::Read read(AVCLAN_frame_t *in, log_t print);
  Error::Send send(const AVCLAN_frame_t *out, log_t print);

private:
  Bus bus;
  const uint16_t address;
};
} // namespace avclan
