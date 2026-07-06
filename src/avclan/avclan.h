// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdint.h>

#ifdef __cplusplus
  #define AVCLAN_ENUM_CLASS class
namespace avclan::detail {
struct Error {
#else
  #define AVCLAN_ENUM_CLASS
#endif

  // Error enums are ordered such that a lower numeric value corresponds to
  // more progress/success before an error occured, with 0 being no errors
  enum AVCLAN_ENUM_CLASS Read : uint8_t {
    BAD_DATA_PARITY = 0x01,
    BAD_LENGTH_RANGE,
    BAD_LENGTH_PARITY,
    BAD_PERIPHERAL_PARITY,
    BAD_CONTROLLER_PARITY,
    BAD_CONTROL_PARITY,
    BAD_PARITY, // non-specific bad parity
    STARTBIT_TOO_SHORT,
    STARTBIT_TOO_LONG,
    BAD_STARTBIT,
  };

  enum AVCLAN_ENUM_CLASS Send : uint8_t {
    NAK_DATA = 0x01,
    NAK_MESSAGE_LENGTH,
    NAK_CONTROL,
    NAK_ADDRESS,
    NAK, // non-specific NAK
    BUSY,
    MUTED,
  };

#ifdef __cplusplus
};
#endif

enum AVCLAN_ENUM_CLASS Bit : uint8_t {
  bit_zero = 0x00,
  bit_one = 0x01,
  bit_start = 0x10
};

#ifdef __cplusplus
} // namespace avclan::detail
#endif

#undef AVCLAN_ENUM_CLASS
