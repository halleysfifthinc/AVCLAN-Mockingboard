// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdint.h>

#ifdef __cplusplus
  #define AVCLAN_ENUM_CLASS class

  #include <type_traits>
  #include <utility>

namespace avclan {

  #if defined(__cpp_lib_to_underlying) && __cpp_lib_to_underlying >= 202102L
using std::to_underlying;
  #else
template <class Enum>
  requires std::is_enum_v<Enum>
constexpr std::underlying_type_t<Enum> to_underlying(Enum e) noexcept {
  return static_cast<std::underlying_type_t<Enum>>(e);
}
  #endif

#else
  #define AVCLAN_ENUM_CLASS
#endif

enum AVCLAN_ENUM_CLASS Action : uint8_t {
  // LAN related
  List_Functions_Req = 0x00,
  List_Functions_Resp = 0x10,
  Lan_Init = 0x01,
  Lan_Init_Complete = 0x58,
  // Lan_Startup_Complete = 0x58,
  Lancheck_End_Req = 0x08,
  Lancheck_End_Resp = 0x18,
  Lancheck_Scan_Req = 0x0a,
  Lancheck_Scan_Resp = 0x1a,
  Lancheck_Req = 0x0c,
  Lancheck_Resp = 0x1c,
  // Lancheck_UNK_Req = 0x0d,
  // Lancheck_UNK_Resp = 0x1d,
  Ping_Req = 0x20,
  Ping_Resp = 0x30,

  // Device switching
  Enable_Function_Req = 0x42,
  Enable_Function_Resp = 0x52,
  Disable_Function_Req = 0x43,
  Disable_Function_Resp = 0x53,

  Advertise_Function = 0x45,
  General_Query = 0x46,

  // Events
  Insertion = 0x50,
  Ejection = 0x51,

  // Physical interface
  Backlight_Adjust = 0x59,
  Beep = 0x60,
  Screen_Press = 0x78,
  Eject = 0x80,
  Disc_Up = 0x90,
  Disc_Down = 0x91,
  Track_Seek_Up = 0x94,
  Track_Seek_Down = 0x95,
  Track_Fast_Forward = 0x98,
  Track_Rewind = 0x99,
  Pwrvol_Knob_Righthand_Turn = 0x9c,
  Pwrvol_Knob_Lefthand_Turn = 0x9d,
  Tape_Not_Ready = 0x9f,
  CD_Enable_Repeat = 0xa0,
  CD_Disable_Repeat = 0xa1,
  CD_Enable_Disk_Repeat = 0xa3,
  CD_Disable_Disk_Repeat = 0xa4,
  CD_Enable_Scan = 0xa6,
  CD_Disable_Scan = 0xa7,
  CD_Enable_Disk_Scan = 0xa9,
  CD_Disable_Disk_Scan = 0xaa,
  CD_Enable_Random = 0xb0,
  CD_Disable_Random = 0xb1,
  CD_Enable_Disk_Random = 0xb3,
  CD_Disable_Disk_Random = 0xb4,

  // Requests and Response pairs
  Initial_Report_Req = 0xe0,
  Initial_Report_Resp = 0xf0,

  Playback_Req = 0xe2,
  Playback_Resp = 0xf2,

  Loading_Req = 0xe4,
  Loading_Resp = 0xf4,

  Track_Name_Req = 0xed,
  Track_Name_Resp = 0xfd,

  // Reports
  Playback_Status = 0xf1,         // Typically unprompted, sent to Device::STATUS
  Loading_Status = 0xf3, // Typically unprompted, sent to Device::STATUS
  Report_TOC = 0xf9,
};

#ifdef __cplusplus
namespace detail {
struct Error {
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
} // namespace detail
} // namespace avclan
#endif

#undef AVCLAN_ENUM_CLASS
