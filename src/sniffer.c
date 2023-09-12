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

#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/sfr_defs.h>
#include <avr/xmega.h>
#include <stdint.h>

#include "avclandrv.h"
#include "com232.h"

#define EV_NOTHING 0
#define EV_STATUS 4

uint8_t Event;
uint8_t echoCharacters;

const char const *offon[] = {"OFF", "ON"};

void Setup();
void general_GPIO_init();
void print_help();

int main() {
  uint8_t readSeq = 0;
  uint8_t s_len = 0;
  uint8_t s_dig = 0;
  uint8_t s_c[2];
  uint8_t i;
  uint8_t data_tmp[32];
  AVCLAN_frame_t msg = {
      .broadcast = UNICAST,
      .controller_addr = CD_ID,
      .control = 0xF,
      .data = data_tmp,
  };

  Setup();
  print_help();

  while (1) {

    if (INPUT_IS_SET) { // if message from some device on AVCLan begin
      AVCLAN_readframe();
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
      cli();
      readkey = RS232_RxCharBuffer[RS232_RxCharBegin];
      RS232_RxCharBegin++;
      if (RS232_RxCharBegin == RS232_RxCharEnd)  // if buffer is consumed
        RS232_RxCharBegin = RS232_RxCharEnd = 0; // reset buffer
      sei();
      switch (readkey) {
        case '?':
          print_help();
          break;
        case 'S': // Read sequence
          printAllFrames = 0;
          RS232_Print("READ SEQUENCE > \n");
          readSeq = 1;
          s_len = 0;
          s_dig = 0;
          s_c[0] = s_c[1] = 0;
          break;
        case 'W': // Send command
          printAllFrames = 1;
          readSeq = 0;
          msg.broadcast = UNICAST;
          msg.length = s_len;
          AVCLAN_sendframe(&msg);
          break;
        case 'Q': // Send broadcast
          printAllFrames = 1;
          readSeq = 0;
          msg.broadcast = BROADCAST;
          msg.peripheral_addr = 0x1FF;
          msg.length = s_len;
          AVCLAN_sendframe(&msg);
          msg.peripheral_addr = HU_ID;
          break;
        case 'R': // Register and wait for a response
          RS232_Print("REGIST:\n");
          AVCLan_Register();
          TCB1.CNT = 0;
          while (TCB1.CNT < 540) {}
          CHECK_AVC_LINE;
          break;
        case 'r': // Register into the abyss
          AVCLan_Register();
          break;
        case 'v':
          verbose ^= 1;
          RS232_Print("Verbose: ");
          RS232_Print(offon[verbose]);
          RS232_Print("\n");
          break;
        case 'l': // Print received messages
          printAllFrames ^= 1;
          RS232_Print("Logging:");
          RS232_Print(offon[printAllFrames]);
          RS232_Print("\n");
          break;
        case 'k': // Echo input
          echoCharacters ^= 1;
          RS232_Print("Echo characters:");
          RS232_Print(offon[echoCharacters]);
          RS232_Print("\n");
          break;
        case 'B': // Beep
          data_tmp[0] = 0x00;
          data_tmp[1] = 0x5E;
          data_tmp[2] = 0x29;
          data_tmp[3] = 0x60;
          data_tmp[4] = 0x01;
          s_len = 5;
          msg.length = s_len;
          msg.broadcast = UNICAST;
          AVCLAN_sendframe(&msg);
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
            if (echoCharacters) {
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
  CD_ID = 0x360;
  HU_ID = 0x190;

  printAllFrames = 1;
  echoCharacters = 1;

  _PROTECTED_WRITE(CLKCTRL.MCLKCTRLB, (CLK_PRESCALE | CLK_PRESCALE_DIV));

  general_GPIO_init();

  // Setup RTC as 1 sec periodic timer
  loop_until_bit_is_clear(RTC_STATUS, RTC_CTRLABUSY_bp);
  RTC.CTRLA = RTC_PRESCALER_DIV1_gc;
  RTC.CLKSEL = RTC_CLKSEL_INT32K_gc;
  RTC.PITINTCTRL = RTC_PI_bm;
  loop_until_bit_is_clear(RTC_PITSTATUS, RTC_CTRLBUSY_bp);
  RTC.PITCTRLA = RTC_PERIOD_CYC32768_gc | RTC_PITEN_bm;

  RS232_Init();

  AVCLAN_init();

  Event = EV_NOTHING;
  sei();
}

/* Configure pin settings which are not configured by peripherals */
void general_GPIO_init() {
  // Set pins PC2-3, PB0,3-5 as inputs
  PORTC.DIRCLR = (PIN2_bm | // Unconnected
                  PIN3_bm); // CTS
  PORTB.DIRCLR = (PIN0_bm | // Unconnected
                  PIN3_bm | // IGN_SENSE
                  PIN4_bm | // Unused, but connected to WOC (PC0)
                  PIN5_bm); // Unused, but connected to WOD (PC1)

  // Enable pull-up resistor and disable input buffer (reduces any EM caused pin
  // toggling and saves power) for unused and unconnected pins
  PORTC.PIN2CTRL = PORT_PULLUPEN_bm | PORT_ISC_INPUT_DISABLE_gc;
  PORTB.PIN0CTRL = PORT_PULLUPEN_bm | PORT_ISC_INPUT_DISABLE_gc;

  // Output only pins: PA3-5, PB1-2,4-5; PC0-1
  // TODO: TxD (PA1), RTS (PA3) is output only, test if RxD needs the input
  // buffer or if the UART peripheral bypasses it
  PORTA.PIN3CTRL = PORT_ISC_INPUT_DISABLE_gc; // RTS
  PORTA.PIN4CTRL = PORT_ISC_INPUT_DISABLE_gc; // WOA
  PORTA.PIN5CTRL = PORT_ISC_INPUT_DISABLE_gc; // WOB
  PORTB.PIN1CTRL = PORT_ISC_INPUT_DISABLE_gc; // MIC_CONTROL
  PORTB.PIN4CTRL = PORT_ISC_INPUT_DISABLE_gc; // non-driving WOC
  PORTB.PIN5CTRL = PORT_ISC_INPUT_DISABLE_gc; // non-driving WOD
  PORTC.PIN0CTRL = PORT_ISC_INPUT_DISABLE_gc; // WOC
  PORTC.PIN1CTRL = PORT_ISC_INPUT_DISABLE_gc; // WOD
}

void print_help() {
  RS232_Print("AVCLAN Mockingboard v1\n");
  RS232_Print("S - read sequence\n"
              "W - send command\n"
              "Q - send broadcast\n"
              "l - Toggle message logging\n"
              "k - Toggle character echo\n"
              "R/r - register device\n"
              "B - Beep\n"
              "v - Toggle verbose logging\n"
#ifdef SOFTWARE_DEBUG
              "M - Measure bit-timing (pulse-widths and periods)\n"
#endif
#ifdef HARDWARE_DEBUG
              "1 - Hold High/low\n"
              "E - Print line status\n"
#endif
              "? - Print this message\n");
}

/* Increment packed 2-digit BCD number.
   WARNING: Overflow behavior is incorrect (e.g. `incBCD(0x99) != 0x00`) */
uint8_t incBCD(uint8_t data) {
  if ((data & 0x9) == 0x9)
    return (data + 7);

  return (data + 1);
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
