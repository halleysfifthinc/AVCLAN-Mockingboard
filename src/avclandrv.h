/*
  Copyright (C) 2015 Allen Hill <allenofthehills@gmail.com>.

  Portions of the following source code are:
  Copyright (C) 2006 Marcin Slonicki <marcin@softservice.com.pl>.

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software Foundation,
  Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

 -----------------------------------------------------------------------
  this file is a part of the TOYOTA Corolla MP3 Player Project
 -----------------------------------------------------------------------
     http://www.softservice.com.pl/corolla/avc

 May 28 / 2009	- version 2

*/

#ifndef __AVCLANDRV_H
#define __AVCLANDRV_H

#include "GlobalDef.h"

#define STOPEvent                                                              \
  cbi(RTC.PITINTCTRL, RTC_PI_bp);                                              \
  cbi(USART0.CTRLA, USART_RXCIE_bp);
#define STARTEvent                                                             \
  sbi(RTC.PITINTCTRL, RTC_PI_bp);                                              \
  sbi(USART0.CTRLA, USART_RXCIE_bp);

#define CHECK_AVC_LINE                                                         \
  if (INPUT_IS_SET)                                                            \
    AVCLan_Read_Message();

void AVC_HoldLine();
void AVC_ReleaseLine();

#define MAXMSGLEN 32

extern uint16_t CD_ID; // CD Changer ID
extern uint16_t HU_ID; // Head-unit ID

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
} commands;

typedef enum { stStop = 0, stPlay = 1 } cd_modes;
extern cd_modes CD_Mode;

extern uint8_t broadcast;
extern uint16_t sender;
extern uint16_t responder;
extern uint8_t message_len;
extern uint8_t message[MAXMSGLEN];

extern uint8_t data_control;
extern uint8_t data_len;
extern uint8_t data[MAXMSGLEN];

uint8_t AVCLan_Read_Message();
void AVCLan_Send_Status();

void AVCLan_Init();
void AVCLan_Register();
uint8_t AVCLan_SendData();
uint8_t AVCLan_SendAnswer();
uint8_t AVCLan_SendDataBroadcast();
uint8_t AVCLan_Command(uint8_t command);

uint8_t incBCD(uint8_t data);

extern uint8_t check_timeout;

extern uint8_t cd_Disc;
extern uint8_t cd_Track;
extern uint8_t cd_Time_Min;
extern uint8_t cd_Time_Sec;

extern uint8_t playMode;

uint8_t AVCLan_SendMyData(uint8_t *data_tmp, uint8_t s_len);
uint8_t AVCLan_SendMyDataBroadcast(uint8_t *data_tmp, uint8_t s_len);

void ShowInMessage();
void ShowOutMessage();

#ifdef SOFTWARE_DEBUG
void AVCLan_Measure();
#endif
#ifdef HARDWARE_DEBUG
void SetHighLow();
#endif

extern uint8_t answerReq;

#endif // __AVCLANDRV_H
