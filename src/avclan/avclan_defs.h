/*
                        AVCLAN-Mockingboard
    Copyright (C) 2015 Allen Hill <allenofthehills@gmail.com>

    Portions of the following source code are based on code that is
    copyright (C) 2006 Marcin Slonicki <marcin@softservice.com.pl>
    copyright (C) 2007 Louis Frigon

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

// Shared cross-cutting AVC-LAN definitions used by two or more layers
// (phy / frame / protocol / cdchanger). This is a leaf header: it must not
// include any other project header.

#ifndef AVCLAN_DEFS_H
#define AVCLAN_DEFS_H

#include <stdbool.h>
#include <stdint.h>

#define MAXMSGLEN 32

#define DEVICE_ADDR 0x360 // CD Changer address
#define HU_ADDR     0x190 // Head-unit address

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

typedef struct print_struct {
  bool print : 1;   // print at all
  bool binary : 1;  // when also printing, format as binary instead of text
  bool verbose : 1; // include extra context in error reports
} log_t;

typedef struct AVCLAN_frame_struct {
  bool is_unicast;
  uint16_t controller_addr; // formerly "master"
  uint16_t peripheral_addr; // formerly "slave"
  uint8_t control;
  uint8_t length;
  uint8_t *data;
} AVCLAN_frame_t;

#endif // AVCLAN_DEFS_H
