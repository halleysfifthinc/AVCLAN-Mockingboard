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

#define CHECK_AVC_LINE                                                         \
  if (!BUS_IS_IDLE)                                                            \
    AVCLAN_readframe();

#define MAXMSGLEN 32

extern uint16_t CD_ID; // CD Changer ID
extern uint16_t HU_ID; // Head-unit ID

extern uint8_t printAllFrames;
extern uint8_t verbose;
extern uint8_t printBinary;

typedef enum {
  cm_Null = 0,
  cm_Status1 = 1,
  cm_Status2 = 2,
  cm_Status3 = 3,
  cm_Status4 = 4,
  cm_PlayReq1 = 5,
  cm_PlayReq2 = 6,
  cm_PlayReq3 = 7,
  cm_StopReq = 8,
  cm_StopReq2 = 9,
  cm_Register = 100,
  cm_Init = 101,
  cm_Check = 102,
  cm_PlayIt = 103,
  cm_Beep = 110,
  cm_NextTrack = 120,
  cm_PrevTrack = 121,
  cm_NextDisc = 122,
  cm_PrevDisc = 123,
  cm_ScanModeOn = 130,
  cm_ScanModeOff = 131,
  cm_CDStatus,
} commands;

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
  cd_state state;
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
  int : 2;
  uint8_t flags2;
} AVCLAN_CD_Status_t;

typedef enum { stStop = 0, stPlay = 1 } cd_modes;

typedef enum MSG_TYPE { BROADCAST = 0, UNICAST = 1 } MSG_TYPE_t;

typedef struct AVCLAN_KnownMessage_struct {
  MSG_TYPE_t broadcast;
  uint8_t length;
  uint8_t data[11];
} AVCLAN_KnownMessage_t;

typedef struct AVCLAN_frame_struct {
  uint8_t valid;
  MSG_TYPE_t broadcast;     // 0 for broadcast messages
  uint16_t controller_addr; // formerly "master"
  uint16_t peripheral_addr; // formerly "slave"
  uint8_t control;
  uint8_t length;
  uint8_t *data;
} AVCLAN_frame_t;

uint8_t AVCLAN_readframe();
uint8_t AVCLAN_sendframe(const AVCLAN_frame_t *frame);

uint8_t AVCLAN_responseNeeded();

void AVCLAN_printframe(const AVCLAN_frame_t *frame, uint8_t binary);
AVCLAN_frame_t *AVCLAN_parseframe(const uint8_t *bytes, uint8_t len);

void AVCLAN_init();
void AVCLan_Send_Status();
void AVCLan_Register();
uint8_t AVCLan_SendAnswer();

#ifdef SOFTWARE_DEBUG
void AVCLan_Measure();
#endif
#ifdef HARDWARE_DEBUG
void SetHighLow();
#endif

#endif // __AVCLANDRV_H
