/*
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

#include <avr/interrupt.h>
#include <avr/io.h>

#include "GlobalDef.h"
#include "avclandrv.h"
#include "com232.h"

void Setup();

byte rcv_command[5];
byte rcv_pos = 0;
byte rcv_time_clr = 0;

int main() {
  byte readSeq = 0;
  byte s_len = 0;
  byte s_dig = 0;
  byte s_c[2];
  byte i;
  byte data_tmp[32];

  Setup();

  RS232_Print("AVCLan reader 1.00\nReady\n\n");
  RS232_Print("\nS - read sequence\nW - send command\nQ - send "
              "broadcast\nL/l - log on/off\nK/k - seq. echo on/off\n");
  RS232_Print("R/r - register device\nB - Beep\n");
#ifdef HARDWARE_DEBUG
  RS232_Print("1 - Hold High/low\nE - Print line status\n");
#endif
#ifdef SOFTWARE_DEBUG
  RS232_Print("M - Measure high and low lengths\n");
#endif

  while (1) {

    if (INPUT_IS_SET) { // if message from some device on AVCLan begin
      AVCLan_Read_Message();
      // show message
    } else {
      // check command from HU
      if (answerReq != 0)
        AVCLan_SendAnswer();
    }

    // HandleEvent
    switch (Event) {
      case EV_STATUS:
        Event &= ~EV_STATUS;
        AVCLan_Send_Status();
        break;
    }

    // Key handler
    if (RS232_RxCharEnd) {
      cbi(USART0.CTRLA, USART_RXCIE_bp); // disable RX complete interrupt
      readkey = RS232_RxCharBuffer[RS232_RxCharBegin]; // read begin of received
                                                       // Buffer
      RS232_RxCharBegin++;
      if (RS232_RxCharBegin == RS232_RxCharEnd)  // if Buffer is empty
        RS232_RxCharBegin = RS232_RxCharEnd = 0; // reset Buffer
      sbi(USART0.CTRLA, USART_RXCIE_bp);         // enable RX complete interrupt
      switch (readkey) {
        case 'S':
          showLog = 0;
          RS232_Print("READ SEQUENCE > \n");
          readSeq = 1;
          s_len = 0;
          s_dig = 0;
          s_c[0] = s_c[1] = 0;
          break;
        case 'W':
          showLog = 1;
          readSeq = 0;
          AVCLan_SendMyData(data_tmp, s_len);
          break;
        case 'Q':
          showLog = 1;
          readSeq = 0;
          AVCLan_SendMyDataBroadcast(data_tmp, s_len);
          break;
        case 'R':
          RS232_Print("REGIST:\n");
          AVCLan_Command(cmRegister);
          TCB1.CNT = 0;
          while (TCB1.CNT < 540) {}
          CHECK_AVC_LINE;
          break;
        case 'r':
          AVCLan_Register();
          break;
        case 'l':
          RS232_Print("Log OFF\n");
          showLog = 0;
          break;
        case 'L':
          RS232_Print("Log ON\n");
          showLog = 1;
          break;
        case 'k':
          RS232_Print("str OFF\n");
          showLog2 = 0;
          break;
        case 'K':
          RS232_Print("str ON\n");
          showLog2 = 1;
          break;
        case 'B':
          data_tmp[0] = 0x00;
          data_tmp[1] = 0x5E;
          data_tmp[2] = 0x29;
          data_tmp[3] = 0x60;
          data_tmp[4] = 0x01;
          s_len = 5;
          AVCLan_SendMyData(data_tmp, s_len);
          break;

#ifdef HARDWARE_DEBUG
        case '1':
          SetHighLow();
          break;
        case 'E':
          if (INPUT_IS_SET) {
            RS232_Print("Set/High/1\n");
          } else if (INPUT_IS_CLEAR) {
            RS232_Print("Unset/Low/0\n");
          } else {
            RS232_Print("WTF?\n");
          }
          break;
#endif
#ifdef SOFTWARE_DEBUG
        case 'M':
          AVCLan_Measure();
          break;
#endif

        default:
          if (readSeq == 1) {
            s_c[s_dig] = readkey;

            s_dig++;
            if (s_dig == 2) {
              if (s_c[0] < ':')
                s_c[0] -= 48;
              else
                s_c[0] -= 55;
              data_tmp[s_len] = 16 * s_c[0];
              if (s_c[1] < ':')
                s_c[1] -= 48;
              else
                s_c[1] -= 55;
              data_tmp[s_len] += s_c[1];
              s_len++;
              s_dig = 0;
              s_c[0] = s_c[1] = 0;
            }
            if (showLog2) {
              RS232_Print("CURRENT SEQUENCE > ");
              for (i = 0; i < s_len; i++) {
                RS232_PrintHex8(data_tmp[i]);
                RS232_SendByte(' ');
              }
              RS232_Print("\n");
            }
          }
      } // switch (readkey)
    }   // if (RS232_RxCharEnd)
  }
  return 0;
}

void Setup() {
  CD_ID_1 = 0x03;
  CD_ID_2 = 0x60;

  HU_ID_1 = 0x01;
  HU_ID_2 = 0x90;

  showLog = 1;
  showLog2 = 1;

  // Default is zero; resetting/zeroing unnecessary
  //  MCUCR = 0;

  loop_until_bit_is_clear(RTC_STATUS, RTC_CTRLABUSY_bp);
  RTC.CTRLA = RTC_PRESCALER_DIV1_gc;
  RTC.CLKSEL = RTC_CLKSEL_INT32K_gc;
  RTC.PITINTCTRL = RTC_PI_bm;
  loop_until_bit_is_clear(RTC_PITSTATUS, RTC_CTRLBUSY_bp);
  RTC.PITCTRLA = RTC_PERIOD_CYC32768_gc | RTC_PITEN_bm;

  RS232_Init();

  AVCLan_Init();

  Event = EV_NOTHING;
  sei();
}

// Periodic interrupt with a 1 sec period
ISR(RTC_PIT_vect) {
  if (CD_Mode == stPlay) {
    cd_Time_Sec = incBCD(cd_Time_Sec);
    if (cd_Time_Sec == 0x60) {
      cd_Time_Sec = 0;
      cd_Time_Min = incBCD(cd_Time_Min);
      if (cd_Time_Min == 0xA0) {
        cd_Time_Min = 0x0;
      }
    }
  }
  Event |= EV_STATUS;
  RTC.PITINTFLAGS |= RTC_PI_bm;
}
