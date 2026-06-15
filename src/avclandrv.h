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

#ifndef __AVCLANDRV_H
#define __AVCLANDRV_H

// AVC LAN bus on AC2 (PA6/7)
// PA6 AINP0 +
// PA7 AINN1 -
#define BUS_IS_IDLE (bit_is_clear(AC2_STATUS, AC_STATE_bp))

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

  // Physical interface
  Backlight_Adjust = 0x59,
  Eject = 0x80,
  Disc_Up = 0x90,
  Disc_Down = 0x91,
  Pwrvol_Knob_Righthand_Turn = 0x9c,
  Pwrvol_Knob_Lefthand_Turn = 0x9d,
  Track_Seek_Up = 0x94,
  Track_Seek_Down = 0x95,
  Track_Fast_Forward = 0x98,
  Track_Rewind = 0x99,
  CD_Enable_Scan = 0xa6,
  CD_Disable_Scan = 0xa7,
  CD_Enable_Disk_Scan = 0xa9,
  CD_Disable_Disk_Scan = 0xaa,
  CD_Enable_Repeat = 0xa0,
  CD_Disable_Repeat = 0xa1,
  CD_Enable_Disk_Repeat = 0xa3,
  CD_Disable_Disk_Repeat = 0xa4,
  CD_Enable_Random = 0xb0,
  CD_Disable_Random = 0xb1,
  CD_Enable_Disk_Random = 0xb3,
  CD_Disable_Disk_Random = 0xb4,

  // Events
  Insertion = 0x50,
  Ejection = 0x51,

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

typedef enum : uint8_t {
  cd_OPEN = 0x01,
  cd_ERR1 = 0x02,
  cd_SEEKING = 0x08,
  cd_PLAYBACK = 0x10,
  cd_SEEKING_TRACK = 0x20,
  cd_LOADING = 0x80,
} cd_state;

typedef enum : uint8_t {
  cd_CD1 = 1 << 0,
  cd_CD2 = 1 << 1,
  cd_CD3 = 1 << 2,
  cd_CD4 = 1 << 3,
  cd_CD5 = 1 << 4,
  cd_CD6 = 1 << 5,
} cd_present_t;

typedef enum : uint8_t {
  cd_DISK_RANDOM = 1 << 1,
  cd_RANDOM = 1 << 2,
  cd_DISK_REPEAT = 1 << 3,
  cd_REPEAT = 1 << 4,
  cd_DISK_SCAN = 1 << 5,
  cd_SCAN = 1 << 6,
} cd_flag_t;

typedef struct AVCLAN_CD_Status {
  uint8_t cds;
  uint8_t state;
  uint8_t disc;
  uint8_t track; // Decimal storage; serialize to BCD
  uint8_t mins;  // Decimal storage; serialize to BCD
  uint8_t secs;  // Decimal storage; serialize to BCD
  uint8_t flags;
  uint8_t flags2;
} AVCLAN_CD_Status_t;

typedef enum : uint8_t { stStop = 0, stPlay = 1 } cd_modes;

typedef enum : uint8_t {
  r_Nothing = 0x00,
  r_Handled,             // No follow-up needed
  r_StatusReport = 0x02, // Needs follow-up status report
  r_NormalizeState,      // cd_status needs normalized and resent
  r_StartPlaying,        // started playing; send current status and then
                         // normalize
  r_TrackChange,         // Time needs reset
} response_t;

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

typedef struct RFrame_struct {
  response_t r;
  AVCLAN_frame_t *frame;
} RFrame_t;

void AVCLAN_init();
void AVCLAN_muteDevice(bool mute);

uint8_t AVCLAN_readframe(AVCLAN_frame_t *frame, log_t print);
response_t AVCLAN_handleframe(const AVCLAN_frame_t *in, AVCLAN_frame_t *out);
uint8_t AVCLAN_sendframe(const AVCLAN_frame_t *frame, log_t print);
RFrame_t *AVCLAN_statemachine(RFrame_t *resp);
// uint8_t AVCLAN_tryrespond(const AVCLAN_frame_t *frame);
void AVCLAN_printframe(const AVCLAN_frame_t *frame, bool binary);
uint8_t AVCLAN_parseframe(const uint8_t *bytes, uint8_t len,
                          AVCLAN_frame_t *frame);
AVCLAN_frame_t *AVCLAN_getStatusFrame();
void AVCLAN_generateStatus(AVCLAN_frame_t *status);

bool AVCLAN_isPlaying();
void AVCLAN_incrementTime();
void AVCLAN_setTime(uint8_t mins, uint8_t secs);
void AVCLAN_normalizeState();

#ifdef SOFTWARE_DEBUG
void AVCLan_Measure();
#endif
#ifdef HARDWARE_DEBUG
void SetHighLow();
#endif

#endif // __AVCLANDRV_H
