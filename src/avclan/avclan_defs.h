// copyright (C) 2006 Marcin Slonicki <marcin@softservice.com.pl>
// copyright (C) 2007 Louis Frigon
// Copyright (C) 2015 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Shared cross-cutting AVC-LAN definitions used by two or more layers
// (phy / frame / protocol / cdchanger). This is a leaf header: it must not
// include any other project header.

#pragma once

#include <stdint.h>

typedef enum : uint8_t {
  dev_LAN = 0x00,
  dev_COMM_CTRL = 0x01,
  dev_COMM_v1 = 0x11,
  dev_COMM_v2 = 0x12,
  dev_SW = 0x21,
  dev_SW_NAME = 0x23,
  dev_SW_CONVERTING = 0x24,
  dev_CMD_SW = 0x25,
  dev_STATUS = 0x31,
  dev_BEEP_HU = 0x28,
  dev_BEEP_SPEAKERS = 0x29,
  dev_TUNER = 0x60,
  dev_TAPE_DECK = 0x61,
  dev_CD = 0x62,
  dev_CD_CHANGER = 0x63,
  dev_AUDIO_AMP = 0x74,
} devices;

typedef enum : uint8_t {
  // LAN related
  List_Functions_Req = 0x00,
  List_Functions_Resp = 0x10,
  Restart_Lan = 0x01,
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

  Current_Function = 0x45,
  General_Query = 0x46,

  // Events
  Insertion = 0x50,
  Ejection = 0x51,

  // Physical interface
  Backlight_Adjust = 0x59,
  Beep = 0x60,
  Eject = 0x80,
  Disc_Up = 0x90,
  Disc_Down = 0x91,
  Track_Seek_Up = 0x94,
  Track_Seek_Down = 0x95,
  Track_Fast_Forward = 0x98,
  Track_Rewind = 0x99,
  Pwrvol_Knob_Righthand_Turn = 0x9c,
  Pwrvol_Knob_Lefthand_Turn = 0x9d,
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
  Initial_Report_Request = 0xe0,
  Initial_Report_Response = 0xf0,

  Playback_Request = 0xe2,
  Playback_Report = 0xf2,

  Loading_Request2 = 0xe4,
  Loading_Response2 = 0xf4,

  Request_Track_Name = 0xed,
  Report_Track_Name = 0xfd,

  // Reports
  Status_Report = 0xf1,         // Typically unprompted, sent to dev_STATUS
  Loading_Status_Report = 0xf3, // Typically unprompted, sent to dev_STATUS
  Report_TOC = 0xf9,
} actions;

// A single bus symbol. bit_zero/bit_one carry data (and double as parity
// values); bit_start marks a frame start bit.
typedef enum avclan_bit : uint8_t {
  bit_zero = 0x00,
  bit_one = 0x01,
  bit_start = 0x10
} avclan_bit_t;
