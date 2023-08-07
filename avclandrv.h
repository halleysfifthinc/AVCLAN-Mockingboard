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
//------------------------------------------------------------------------------
#include "GlobalDef.h"

#define STOPEvent  cbi(RTC.PITINTCTRL, RTC_PI_bp); cbi(USART0.CTRLA, USART_RXCIE_bp);
#define STARTEvent sbi(RTC.PITINTCTRL, RTC_PI_bp); sbi(USART0.CTRLA, USART_RXCIE_bp);


#define CHECK_AVC_LINE		if (INPUT_IS_SET) AVCLan_Read_Message();

void AVC_HoldLine();
void AVC_ReleaseLine();

#define MAXMSGLEN	32

// Head Unid ID
extern byte HU_ID_1;		//	0x01
extern byte HU_ID_2;		//	0x40

extern byte CD_ID_1;		// 0x03
extern byte CD_ID_2;		// 0x60


// DVD CHANGER
//#define CD_ID_1	0x02
//#define CD_ID_2	0x50

#define cmNull		0
#define cmStatus1	1
#define cmStatus2	2
#define cmStatus3	3
#define cmStatus4	4


#define cmRegister		100
#define cmInit			101
#define cmCheck			102
#define cmPlayIt		103
#define cmBeep			110

#define cmNextTrack		120
#define cmPrevTrack		121
#define cmNextDisc		122
#define cmPrevDisc		123

#define cmScanModeOn	130
#define cmScanModeOff	131

#define cmPlayReq1	5
#define cmPlayReq2	6
#define cmPlayReq3	7
#define cmStopReq	8
#define cmStopReq2	9

typedef enum { stStop=0, stPlay=1 } cd_modes;
extern cd_modes CD_Mode;


extern byte broadcast;
extern byte master1;
extern byte master2;
extern byte slave1;
extern byte slave2;
extern byte message_len;
extern byte message[MAXMSGLEN];

extern byte data_control;
extern byte data_len;
extern byte data[MAXMSGLEN];

byte AVCLan_Read_Message();
void AVCLan_Send_Status();

void AVCLan_Init();
void AVCLan_Register();
byte  AVCLan_SendData();
byte  AVCLan_SendAnswer();
byte  AVCLan_SendDataBroadcast();
byte	AVCLan_Command(byte command);

byte  incBCD(byte data);
// byte  decBCD(byte data); // unused
// byte  bin2BCD8(byte data);

extern byte check_timeout;

extern byte cd_Disc;
extern byte cd_Track;
extern byte cd_Time_Min;
extern byte cd_Time_Sec;

extern byte playMode;

byte AVCLan_SendMyData(byte *data_tmp, byte s_len);
byte AVCLan_SendMyDataBroadcast(byte *data_tmp, byte s_len);

void ShowInMessage();
void ShowOutMessage();

#ifdef SOFTWARE_DEBUG
  void AVCLan_Measure();
#endif
#ifdef HARDWARE_DEBUG
  void SetHighLow();
#endif

//------------------------------------------------------------------------------
extern byte answerReq;
//------------------------------------------------------------------------------
#endif
