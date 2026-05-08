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
#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "avclandrv.h"
#include "com232.h"
#include "queue.h"

uint8_t echoCharacters;
uint8_t readBinary;
uint8_t muteBus;
uint8_t readkey;

const char *const offon[] = {"OFF", "ON"};

#define CACHE_SIZE 16

AVCLAN_frame_t frames[CACHE_SIZE];
RFrame_t responses[CACHE_SIZE];
uint8_t framesdata[CACHE_SIZE][MAXMSGLEN];

Queue_t cache, rcache, incoming, outgoing;

volatile uint8_t enqueueStatus = 0;

void Setup();
void general_GPIO_init();
void print_help();

static uint8_t return_resp(RFrame_t *resp) {
  uint8_t err;
  AVCLAN_frame_t *out = resp->frame;
  if ((out >= frames) && (out < &frames[CACHE_SIZE]))
    // only return cache-owned frames (e.g. not status, etc)
    err = pushQueue(&cache, out);
  err = pushQueue(&rcache, resp);
  return err;
}

static uint8_t push_or_return_resp(RFrame_t *resp) {
  uint8_t err = pushQueue(&outgoing, resp) && return_resp(resp);
  return err;
}

int main() {
  uint8_t readSeq = 0;
  uint8_t hexChars[2];
  uint8_t hexDigit = 0; // current digit being written to hexChars

  MSG_TYPE_t seqBroadcast = BROADCAST;
  uint8_t lastPrintAllFrames = 1;

  uint8_t data_tmp[MAXMSGLEN];
  uint8_t seqLen = 0; // current length written to data_tmp

  uint8_t err = 0;

  AVCLAN_frame_t *msg, *out;
  RFrame_t *resp;
  AVCLAN_frame_t *status = AVCLAN_getStatusFrame();

  for (uint8_t i = 0; i < CACHE_SIZE; ++i) {
    frames[i].data = framesdata[i];
    frames[i].control = 0x0f;
  }

  constructQueue(&cache, frames, sizeof(AVCLAN_frame_t), CACHE_SIZE, 1);
  constructEmptyQueue(&incoming, sizeof(AVCLAN_frame_t), CACHE_SIZE);
  constructQueue(&rcache, responses, sizeof(RFrame_t), CACHE_SIZE, 1);
  constructEmptyQueue(&outgoing, sizeof(RFrame_t), CACHE_SIZE);

  Setup();
  print_help();

  while (1) {

    if (!BUS_IS_IDLE) {
      msg = (AVCLAN_frame_t *)(&cache);
      if (!msg) {
        RS232_Print("!! Dropping an incoming message; cache is empty !!");
        continue;
      }

      err = AVCLAN_readframe(msg);
      if (!err)
        err = pushQueue(&incoming, msg) && pushQueue(&cache, msg);
    } else if (!isEmpty(&incoming)) {
      out = (AVCLAN_frame_t *)popQueue(&cache);
      if (!out) {
        RS232_Print("!! Unable to respond; cache is empty !!");
        continue;
      }

      msg = (AVCLAN_frame_t *)popQueue(
          &incoming); // prior !isempty(incoming) guarantees success
      response_t respond = AVCLAN_handleframe(msg, out);

      if (respond) {
        resp = (RFrame_t *)popQueue(&rcache);
        if (resp) {
          *resp = (RFrame_t){.r = respond, .frame = out};
          push_or_return_resp(resp);
        } else
          pushQueue(&cache, out);
      } else // no response needed; return to circulation
        pushQueue(&cache, out);

      pushQueue(&cache, msg);
    } else if (!isEmpty(&outgoing)) {
      resp = (RFrame_t *)popQueue(
          &outgoing); // prior !isempty(outgoing) guarantees success
      out = resp->frame;
      err = AVCLAN_sendframe(out);
      if (err) {
        RS232_Print("!! Failed to send frame; error code ");
        RS232_PrintHex(err);
        RS232_Print(" !!\n");
        return_resp(resp);
      } else {
        // Re-use successful resp for sequence
        switch (resp->r) {
          case r_TrackChange: AVCLAN_setTime(0x00, 0x00); // fallthrough
          case r_NormalizeState:
            AVCLAN_normalizeState(out);
            resp->r = r_Handled;
            push_or_return_resp(resp);
            break;
          case r_StartPlaying:
            AVCLAN_generateStatus(out);
            resp->r = r_NormalizeState;
            push_or_return_resp(resp);
            break;
          case r_StatusReport:
            AVCLAN_generateStatus(out);
            resp->r = r_Handled;
            push_or_return_resp(resp);
            break;
          case r_Nothing: __builtin_unreachable();
          case r_Handled: return_resp(resp); break;
        }
      }
    } else if (enqueueStatus) {
      AVCLAN_generateStatus(status);
      resp = (RFrame_t *)popQueue(&rcache);
      if (resp) {
        *resp = (RFrame_t){.r = r_Handled, .frame = status};
        err = pushQueue(&outgoing, resp);
        if (err) {
          RS232_Print("Outgoing queue full; unable to send status update");
          pushQueue(&rcache, resp);
        } else
          enqueueStatus = 0; // Only clear if successful
      }
      // no further error handling needed; status isn't part of the cache
    }

    // Key handler
    if (RS232_RxCharEnd) {
      cli();
      readkey = RS232_RxCharBuffer[RS232_RxCharBegin++];
      if (RS232_RxCharBegin == RS232_RxCharEnd)  // if buffer is consumed
        RS232_RxCharBegin = RS232_RxCharEnd = 0; // reset buffer
      sei();
      switch (readkey) {
        case '?': print_help(); break;
        case 'v':
          verbose ^= 1;
          RS232_Print("Verbose: ");
          RS232_Print(offon[verbose]);
          RS232_Print("\n");
          break;
        case 'X':
          // X/x isn't a single toggle interface because this is used
          // programmatically and is simpler than reading the toggle
          // state
          printBinary = 1;
          RS232_Print("Binary: ");
          RS232_Print(offon[1]);
          RS232_Print("\n");
          break;
        case 'x':
          printBinary = 0;
          RS232_Print("Binary: ");
          RS232_Print(offon[0]);
          RS232_Print("\n");
          break;
        case 'l': // Print received messages
          printAllFrames ^= 1;
          RS232_Print("Logging: ");
          RS232_Print(offon[printAllFrames]);
          RS232_Print("\n");
          break;
        case 'k': // Echo input
          echoCharacters ^= 1;
          RS232_Print("Echo characters: ");
          RS232_Print(offon[echoCharacters]);
          RS232_Print("\n");
          break;
        case 'm': // Mute mockingboard device on AVCLAN bus
          muteBus ^= 1;
          AVCLAN_muteDevice(muteBus);
          RS232_Print("Mute device: ");
          RS232_Print(offon[muteBus]);
          RS232_Print("\n");
          break;
        case 'E': // Beep
          out = (AVCLAN_frame_t *)popQueue(&cache);
          if (out) {
            resp = (RFrame_t *)popQueue(&rcache);
            if (resp) {
              out->broadcast = UNICAST;
              out->controller_addr = DEVICE_ADDR;
              out->peripheral_addr = HU_ADDR;
              {
                const uint8_t beep[] = {0x00, dev_CD_CHANGER, dev_BEEP_SPEAKERS,
                                        0x60, 0x01};
                out->length = sizeof(beep);
                memcpy(out->data, beep, sizeof(beep));
              }
              *resp = (RFrame_t){.r = r_Handled, .frame = out};
              push_or_return_resp(resp);
            }
          }
          break;
        case 'P':
          out = (AVCLAN_frame_t *)popQueue(&cache);
          if (out) {
            resp = (RFrame_t *)popQueue(&rcache);
            if (resp) {
              out->broadcast = UNICAST;
              out->controller_addr = DEVICE_ADDR;
              out->peripheral_addr = HU_ADDR;
              {
                const uint8_t play[] = {0x00,      dev_COMM_CTRL,  dev_COMM_v1,
                                        Insertion, dev_CD_CHANGER, 0x01};
                out->length = sizeof(play);
                memcpy(out->data, play, sizeof(play));
              }
              *resp = (RFrame_t){.r = r_Handled, .frame = out};
              push_or_return_resp(resp);
            }
          }
          break;

#ifdef SOFTWARE_DEBUG
        case 'M': AVCLan_Measure(); break;
#endif

        case 0x10: // Signals binary sequence incoming
          if (!readSeq && !readBinary) {
            readSeq = readBinary = 1;
            seqLen = 0;
            break;
          } // otherwise we're reading binary and that's a data byte
        case 'U': // Send command
          RS232_Print("READ SEQUENCE (U)> \n");
          lastPrintAllFrames = printAllFrames;
          printAllFrames = 0;
          readSeq = 1;
          seqLen = hexDigit = 0;
          hexChars[0] = hexChars[1] = 0;
          seqBroadcast = UNICAST;
          break;
        case 'B': // Send broadcast
          RS232_Print("READ SEQUENCE (B)> \n");
          lastPrintAllFrames = printAllFrames;
          printAllFrames = 0;
          readSeq = 1;
          seqLen = hexDigit = 0;
          hexChars[0] = hexChars[1] = 0;
          seqBroadcast = BROADCAST;
          break;
        case '\n':
          if (readSeq) {
            if (readBinary) {
              if (data_tmp[seqLen] == 0x17) {
                out = (AVCLAN_frame_t *)popQueue(&cache);
                if (out) {
                  err = AVCLAN_parseframe(data_tmp, --seqLen, out);
                  if (!err) {
                    resp = (RFrame_t *)popQueue(&rcache);
                    if (resp) {
                      *resp = (RFrame_t){.r = r_Handled, .frame = out};
                      push_or_return_resp(resp);
                    }
                  }
                }
                readSeq = readBinary = 0;
              } else
                goto DEFAULT; // reading binary and this is a real data byte;
                              // fall through to default
            } else {
              out = (AVCLAN_frame_t *)popQueue(&cache);
              if (out) {
                resp = (RFrame_t *)popQueue(&rcache);
                if (resp) {
                  out->broadcast = seqBroadcast;
                  out->controller_addr = DEVICE_ADDR;
                  switch (seqBroadcast) {
                    case UNICAST: out->peripheral_addr = HU_ADDR; break;
                    case BROADCAST: out->peripheral_addr = 0x1FF; break;
                  }
                  out->length = seqLen;
                  memcpy(out->data, data_tmp, seqLen);
                  *resp = (RFrame_t){.r = r_Handled, .frame = out};
                  push_or_return_resp(resp);
                }
              }
              printAllFrames = lastPrintAllFrames;
            }
            break;
          }
        DEFAULT:
        default:
          if (readSeq) {
            if (readBinary) {
              data_tmp[seqLen++] = readkey;
            } else {
              hexChars[hexDigit++] = readkey;

              if (hexDigit == 2) {
                char h, l;
                h = toupper(hexChars[0]);
                h += (h < ':') ? 0xd0 : 0xc9; // digit or letter

                l = toupper(hexChars[1]);
                l += (l < ':') ? 0xd0 : 0xc9;

                data_tmp[seqLen++] = (h << 4) | l;
                hexDigit = hexChars[0] = hexChars[1] = 0;
              }
              if (echoCharacters) {
                RS232_Print("CURRENT SEQUENCE > ");
                for (uint8_t i = 0; i < seqLen; i++) {
                  RS232_PrintHex8(data_tmp[i]);
                  RS232_SendByte(' ');
                }
                RS232_Print("\n");
              }
            }
          }
      } // switch (readkey)
    } // if (RS232_RxCharEnd)
  }
  return 0;
}

void Setup() {
  printAllFrames = 1;
  echoCharacters = 1;
  readBinary = 0;
  printBinary = 0;

  _PROTECTED_WRITE(CLKCTRL.MCLKCTRLB, (CLK_PRESCALE | CLK_PRESCALE_DIV));

  general_GPIO_init();
  RS232_Init();
  AVCLAN_init();

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

  // TODO: Remove once IGN_SENSE hardware is fixed
  PORTB.DIRSET = PIN3_bm;
  PORTB.OUTSET = PIN3_bm;

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
  RS232_Print("W - begin reading for unicast message\n"
              "Q - begin reading for broadcast message\n"
              "m - Toggle mute for mockingboard bus activity\n"
              "l - Toggle message logging\n"
              "k - Toggle character echo\n"
              "X/x - Turn binary printing ON or OFF, respectively\n"
              "E - Beep\n"
              "P - Play\n"
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

// Periodic interrupt with a 1 sec period; only enabled when playing
ISR(RTC_PIT_vect) {
  AVCLAN_incrementTime();
  enqueueStatus = 1;
  RTC.PITINTFLAGS = RTC_PI_bm;
}
