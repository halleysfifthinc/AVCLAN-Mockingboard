// copyright (C) 2006 Marcin Slonicki <marcin@softservice.com.pl>
// copyright (C) 2007 Louis Frigon
// Copyright (C) 2015 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "avclandrv.h"
#include "board.h"
#include "com232.h"
#include "queue.h"

const char *const offon[] = {"OFF", "ON"};

#define CACHE_SIZE 16
static_assert((CACHE_SIZE & (CACHE_SIZE - 1)) == 0,
              "CACHE_SIZE must be a power of two (qMask depends on it)");

static AVCLAN_frame_t frames[CACHE_SIZE];
static RFrame_t responses[CACHE_SIZE];
static uint8_t framesdata[CACHE_SIZE][MAXMSGLEN];

static void *cacheSlots[CACHE_SIZE];
static void *rcacheSlots[CACHE_SIZE];
static void *incomingSlots[CACHE_SIZE];
static void *outgoingSlots[CACHE_SIZE];

static Queue_t cache, rcache, incoming, outgoing;

void Setup();
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

static void push_or_return_resp(RFrame_t *resp) {
  // r_Nothing == don't respond/send; should never be added to outgoing
  if (resp->r != r_Nothing && !pushQueue(&outgoing, resp))
    return;

  return_resp(resp);
}

static void toggle_flag(bool *flag, const char *msg) {
  *flag = !*flag;
  RS232_Print(msg);
  RS232_Print(offon[*flag]);
  RS232_Print("\n");
}

static void set_flag(bool *flag, bool val, const char *msg) {
  *flag = val;
  RS232_Print(msg);
  RS232_Print(offon[val]);
  RS232_Print("\n");
}

int main() {
  uint8_t hexChars[2];
  uint8_t hexDigit = 0; // current digit being written to hexChars

  bool readSeq = false;
  bool seqIsUnicast = false;
  bool readBinary = false;

  bool verbose = true;
  bool printAllFrames = true;
  bool lastPrintAllFrames = true;
  bool printBinary = false;
  bool echoCharacters = true;
  bool muteBus = false;

  // Binary-mode REPL includes the full wire preamble (broadcast + 2*addr +
  // control + length), so size for the worst case.
  uint8_t data_tmp[MAXMSGLEN + sizeof(AVCLAN_frame_t)];
  uint8_t seqIdx = 0; // current index in data_tmp

  uint8_t err = 0;
  uint8_t failedStatusReports = 0;

  for (uint8_t i = 0; i < CACHE_SIZE; ++i) {
    frames[i].control = 0x0f;
    frames[i].data = framesdata[i];
  }

  constructQueue(&cache, cacheSlots, frames, sizeof(AVCLAN_frame_t), CACHE_SIZE,
                 true);
  constructEmptyQueue(&incoming, incomingSlots, CACHE_SIZE);

  constructQueue(&rcache, rcacheSlots, responses, sizeof(RFrame_t), CACHE_SIZE,
                 true);
  constructEmptyQueue(&outgoing, outgoingSlots, CACHE_SIZE);

  Setup();
  print_help();

  while (true) {
    if (AVCLAN_busActive()) {
      if (AVCLAN_frame_t *msg = popQueue(&cache)) {
        err = AVCLAN_readframe(msg, (log_t){.print = printAllFrames,
                                            .binary = printBinary,
                                            .verbose = verbose});
        if (!err)
          err = pushQueue(&incoming, msg);

        if (err)
          pushQueue(&cache, msg);
      } else {
        RS232_Print("!! Dropping an incoming message; cache is empty !!\n");
      }
    }

    if (AVCLAN_frame_t *in = peekQueue(&incoming)) {
      if (AVCLAN_frame_t *out = popQueue(&cache)) {
        incrementRead(&incoming); // successful out = pop cache; claim the
                                  // peeked incoming
        response_t respond = AVCLAN_handleframe(in, out);
        pushQueue(&cache, in); // return in after use

        if (respond) {
          if (RFrame_t *resp = popQueue(&rcache)) {
            *resp = (RFrame_t){.r = respond, .frame = out};
            push_or_return_resp(resp);
          } else
            pushQueue(&cache, out); // rcache exhausted — don't leak the frame
        } else                      // no response needed; return to circulation
          pushQueue(&cache, out);
      } else {
        RS232_Print("!! Unable to respond; cache is empty !!\n");
      }
    }

    if (RFrame_t *resp = popQueue(&outgoing)) {
      AVCLAN_frame_t *out = resp->frame;
      err = AVCLAN_sendframe(
          out, (log_t){.print = printAllFrames, .binary = printBinary});
      if (err || resp->r == r_Handled) {
        if (err && out == AVCLAN_getStatusFrame() &&
            ++failedStatusReports > 1) {
          failedStatusReports = 0;
          AVCLAN_stopPlaying(); // Disable periodic updates if e.g. no-one's
                                // listening (car was turned off?)
        }
        return_resp(resp);
      } else {
        resp = AVCLAN_statemachine(resp);
        push_or_return_resp(resp);
      }
    } else if (statustimer_tickPending()) {
      AVCLAN_frame_t *status = AVCLAN_getStatusFrame();
      AVCLAN_generateStatus(status, true, dev_STATUS);
      if (RFrame_t *resp = (RFrame_t *)popQueue(&rcache)) {
        *resp = (RFrame_t){.r = r_Handled, .frame = status};
        err = pushQueue(&outgoing, resp);
        if (err) {
          RS232_Print("Outgoing queue full; unable to send status update\n");
          pushQueue(&rcache, resp);
        } else
          statustimer_clearTick(); // Only clear if successful
      }
      // no further error handling needed; status isn't part of the cache
    }

    // Key handler
    if (RS232_hasChar()) {
      char readkey = RS232_getChar();
      switch (readkey) {
        case '?': print_help(); break;
        case 'v': toggle_flag(&verbose, "Verbose errors: "); break;
        case 'l': toggle_flag(&printAllFrames, "Logging: "); break;
        case 'k': toggle_flag(&echoCharacters, "Echo characters: "); break;
        case 'm':
          toggle_flag(&muteBus, "Mute device: ");
          AVCLAN_muteDevice(muteBus);
          break;

        // X/x isn't a toggle interface because this is used
        // programmatically and is simpler than reading back the toggle
        // state
        case 'X': set_flag(&printBinary, true, "Binary: "); break;
        case 'x': set_flag(&printBinary, false, "Binary: "); break;

        case 'E': // Beep
          if (AVCLAN_frame_t *out = (AVCLAN_frame_t *)popQueue(&cache)) {
            if (RFrame_t *resp = popQueue(&rcache)) {
              out->is_unicast = true;
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
            } else
              pushQueue(&cache, out);
          }
          break;
        case 'P':
          if (AVCLAN_frame_t *out = (AVCLAN_frame_t *)popQueue(&cache)) {
            if (RFrame_t *resp = popQueue(&rcache)) {
              out->is_unicast = true;
              out->controller_addr = DEVICE_ADDR;
              out->peripheral_addr = HU_ADDR;
              {
                const uint8_t play[] = {0x00,     dev_COMM_CTRL,  dev_COMM_v1,
                                        Ejection, dev_CD_CHANGER, 0x01};
                out->length = sizeof(play);
                memcpy(out->data, play, sizeof(play));
              }
              *resp = (RFrame_t){.r = r_Ejection, .frame = out};
              push_or_return_resp(resp);
            } else
              pushQueue(&cache, out);
          }
          break;

#ifndef NDEBUG
        case 'g': AVCLAN_micToggle(); break;
        case 'p':
          RS232_Print("First play/pause begin ... ");
          AVCLAN_mediaFunction(MEDIA_PLAY_PAUSE);
          while (AVCLAN_isMediaFunctioning()) {}
          RS232_Print("end\nSecond play/pause begin ... ");
          AVCLAN_mediaFunction(MEDIA_PLAY_PAUSE);
          while (AVCLAN_isMediaFunctioning()) {}
          RS232_Print("end\n");
          break;
        case 's':
          RS232_Print("Skip begin ... ");
          AVCLAN_mediaFunction(MEDIA_SKIP_FORWARD);
          while (AVCLAN_isMediaFunctioning()) {}
          RS232_Print("end\n");
          break;
        case 'b':
          RS232_Print("Skip back begin ... ");
          AVCLAN_mediaFunction(MEDIA_SKIP_BACKWARD);
          while (AVCLAN_isMediaFunctioning()) {}
          RS232_Print("end\n");
          break;
        case 'M': AVCLan_Measure(); break;
#endif

        case 0x10: // Signals binary sequence incoming
          if (!readSeq && !readBinary) {
            readSeq = readBinary = true;
            seqIdx = 0;
            break;
          } else
            goto DEFAULT; // reading binary and this is a real data byte

        case 'U': // Send command
          RS232_Print("READ SEQUENCE (U)> \n");
          lastPrintAllFrames = printAllFrames;
          printAllFrames = false;
          readSeq = true;
          seqIdx = hexDigit = 0;
          hexChars[0] = hexChars[1] = 0;
          seqIsUnicast = true;
          break;
        case 'B': // Send broadcast
          RS232_Print("READ SEQUENCE (B)> \n");
          lastPrintAllFrames = printAllFrames;
          printAllFrames = false;
          readSeq = true;
          seqIdx = hexDigit = 0;
          hexChars[0] = hexChars[1] = 0;
          seqIsUnicast = false;
          break;
        case '\n':
          if (readSeq) {
            if (readBinary) {
              if (data_tmp[seqIdx] == 0x17) {
                if (AVCLAN_frame_t *out = (AVCLAN_frame_t *)popQueue(&cache)) {
                  if (!AVCLAN_parseframe(data_tmp, --seqIdx, out)) {
                    if (RFrame_t *resp = popQueue(&rcache)) {
                      *resp = (RFrame_t){.r = r_Handled, .frame = out};
                      push_or_return_resp(resp);
                    } else
                      pushQueue(&cache, out);
                  } else
                    pushQueue(&cache, out);
                }
                readSeq = readBinary = false;
              } else
                goto DEFAULT; // reading binary and this is a real data byte;
                              // fall through to default
            } else {
              if (AVCLAN_frame_t *out = (AVCLAN_frame_t *)popQueue(&cache)) {
                if (RFrame_t *resp = popQueue(&rcache)) {
                  out->is_unicast = seqIsUnicast;
                  out->controller_addr = DEVICE_ADDR;
                  out->peripheral_addr = seqIsUnicast ? HU_ADDR : 0x1FF;
                  out->length = seqIdx;
                  memcpy(out->data, data_tmp, seqIdx);
                  *resp = (RFrame_t){.r = r_Handled, .frame = out};
                  push_or_return_resp(resp);
                } else
                  pushQueue(&cache, out);
              }
              printAllFrames = lastPrintAllFrames;
            }
            break;
          }
        DEFAULT:
        default:
          if (readSeq) {
            if (readBinary) {
              data_tmp[seqIdx++] = readkey;
            } else {
              hexChars[hexDigit++] = readkey;

              if (hexDigit == 2) {
                char h, l;
                h = toupper(hexChars[0]);
                h += (h < ':') ? 0xd0 : 0xc9; // digit or letter

                l = toupper(hexChars[1]);
                l += (l < ':') ? 0xd0 : 0xc9;

                data_tmp[seqIdx++] = (h << 4) | l;
                hexDigit = hexChars[0] = hexChars[1] = 0;
              }
              if (echoCharacters) {
                RS232_Print("CURRENT SEQUENCE > ");
                for (uint8_t i = 0; i < seqIdx; i++) {
                  RS232_PrintHex8(data_tmp[i]);
                  RS232_SendByte(' ');
                }
                RS232_Print("\n");
              }
            }
          }
      } // switch (readkey)
    } // if (RS232_hasChar())
  }
  return 0;
}

void Setup() {
  board_init(); // clock + GPIO bring-up (target-specific)
  RS232_Init();
  AVCLAN_init();
  board_interruptsEnable();
}

void print_help() {
  RS232_Print("AVCLAN Mockingboard v1\n");
  RS232_Print("U - begin reading for unicast message\n"
              "B - begin reading for broadcast message\n"
              "m - Toggle mute for mockingboard bus activity\n"
              "v - Toggle verbose error logging\n"
              "l - Toggle message logging\n"
              "X/x - Turn binary logging ON or OFF, respectively\n"
              "k - Toggle character echo\n"
              "E - Beep\n"
              "P - Play\n"
#ifndef NDEBUG
              "g - Toggle MIC_CONTROL high/low\n"
              "p - double MIC play/pause pulse\n" // Confirm pulse function and
                                                  // refractory timing
              "s - MIC skip forward\n"
              "b - MIC skip backward\n"
              "M - Measure bit-timing (pulse-widths and periods)\n"
#endif
              "? - Print this message\n");
}
