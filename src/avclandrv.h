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

#define sbi(port, bit) (port) |= (1 << (bit))  // Set bit (i.e. to 1)
#define cbi(port, bit) (port) &= ~(1 << (bit)) // Clear bit (i.e. set bit to 0)

#define STOPEvent                                                              \
  cbi(RTC.PITINTCTRL, RTC_PI_bp);                                              \
  cbi(USART0.CTRLA, USART_RXCIE_bp);
#define STARTEvent                                                             \
  sbi(RTC.PITINTCTRL, RTC_PI_bp);                                              \
  sbi(USART0.CTRLA, USART_RXCIE_bp);

#define MAXMSGLEN 32

#define DEVICE_ADDR 0x360 // CD Changer address
#define HU_ADDR     0x190 // Head-unit address

extern uint8_t printAllFrames;
extern uint8_t verbose;
extern uint8_t printBinary;

typedef enum {
  cm_Null = 0,
  cm_CDStatus,
} commands;

typedef enum {
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

typedef enum {
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
  Ping_Req = 0x20,
  Ping_Resp = 0x30,

  // Device switching
  Enable_Function_Req = 0x42,
  Enable_Function_Resp = 0x52,
  Disable_Function_Req = 0x43,
  Disable_Function_Resp = 0x53,

  Advertise_Function = 0x45,
  General_Query = 0x46,

  // Physical interface
  Eject = 0x80,
  Disc_Up = 0x90,
  Disc_Down = 0x91,
  Pwrvol_Knob_Righthand_Turn = 0x9c,
  Pwrvol_Knob_Lefthand_Turn = 0x9d,
  Track_Seek_Up = 0x94,
  Track_Seek_Down = 0x95,
  CD_Enable_Scan = 0xa6,
  CD_Disable_Scan = 0xa7,
  CD_Enable_Repeat = 0xa0,
  CD_Disable_Repeat = 0xa1,
  CD_Enable_Random = 0xb0,
  CD_Disable_Random = 0xb1,

  // CD functions
  // Events
  Inserted_CD = 0x50,
  Removed_CD = 0x51,

  // Requests
  Request_Report = 0xe0,
  Request_Report2 = 0xe2,
  Request_Loader2 = 0xe4,
  Request_Track_Name = 0xed,

  // Reports
  Report = 0xf1,
  Report2 = 0xf2,
  Report_Loader = 0xf3,
  Report_Loader2 = 0xf4,
  Report_TOC = 0xf9,
  Report_Track_Name = 0xfd,
} actions;

typedef enum {
  cd_OPEN = 0x01,
  cd_ERR1 = 0x02,
  cd_SEEKING = 0x08,
  cd_PLAYBACK = 0x10,
  cd_SEEKING_TRACK = 0x20,
  cd_LOADING = 0x80,
} cd_state;

typedef struct AVCLAN_CD_Status {
  _Bool cd1 : 1;
  _Bool cd2 : 1;
  _Bool cd3 : 1;
  _Bool cd4 : 1;
  _Bool cd5 : 1;
  _Bool cd6 : 1;
  int : 2;
  uint8_t state;
  uint8_t disc;
  uint8_t track;
  uint8_t mins;
  uint8_t secs;
  int : 1;
  _Bool disk_random : 1;
  _Bool random : 1;
  _Bool disk_repeat : 1;
  _Bool repeat : 1;
  _Bool disk_scan : 1;
  _Bool scan : 1;
  int : 1;
  uint8_t flags2;
} AVCLAN_CD_Status_t;

typedef enum { stStop = 0, stPlay = 1 } cd_modes;

typedef enum MSG_TYPE { BROADCAST = 0, UNICAST = 1 } MSG_TYPE_t;

typedef struct AVCLAN_frame_struct {
  MSG_TYPE_t broadcast;     // 0 for broadcast messages
  uint16_t controller_addr; // formerly "master"
  uint16_t peripheral_addr; // formerly "slave"
  uint8_t control;
  uint8_t length;
  uint8_t *data;
} AVCLAN_frame_t;

void AVCLAN_init();
void AVCLAN_muteDevice(uint8_t mute);

uint8_t AVCLAN_readframe();
uint8_t AVCLAN_sendframe(const AVCLAN_frame_t *frame);

// To allow inlining qEmpty and AVCLAN_responseNeeded
#ifndef VAR_DECLS
  #define _DECL extern
  #define _INIT(x)
#else
  #define _DECL
  #define _INIT(x) = x
#endif
_DECL uint8_t answerReq _INIT(0);
_DECL uint8_t qWrite _INIT(0);
_DECL uint8_t qRead _INIT(0);
extern cd_modes CD_Mode;

inline uint8_t qEmpty() { return (qWrite == qRead); }
inline uint8_t AVCLAN_responseNeeded() { return (answerReq != 0) || !qEmpty(); }

uint8_t AVCLAN_respond();

void AVCLAN_printframe(const AVCLAN_frame_t *frame, uint8_t binary);
AVCLAN_frame_t *AVCLAN_parseframe(const uint8_t *bytes, uint8_t len);

#ifdef SOFTWARE_DEBUG
void AVCLan_Measure();
#endif
#ifdef HARDWARE_DEBUG
void SetHighLow();
#endif

#endif // __AVCLANDRV_H
