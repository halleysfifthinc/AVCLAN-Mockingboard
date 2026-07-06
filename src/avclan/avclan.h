// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#ifdef __cplusplus
  #include <cstdint>
  #define AVCLAN_ENUM_CLASS enum class
#else
  #include <stdint.h>
  #define AVCLAN_ENUM_CLASS enum
#endif

#ifdef __cplusplus
namespace avclan::detail {
struct Error {
#endif

  // Error enums are ordered such that a lower numeric value corresponds to
  // more progress/success before an error occured, with 0 being no errors
  AVCLAN_ENUM_CLASS Read : uint8_t {
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
}
// #ifndef __cplusplus
// Read
// #endif
;

  AVCLAN_ENUM_CLASS Send : uint8_t {
  NAK_DATA = 0x01,
  NAK_MESSAGE_LENGTH,
  NAK_CONTROL,
  NAK_ADDRESS,
  NAK, // non-specific NAK
  BUSY,
  MUTED,
}
// #ifndef __cplusplus
// Send
// #endif
;

#ifdef __cplusplus
};
} // namespace avclan::detail
#endif

#undef AVCLAN_ENUM_CLASS
