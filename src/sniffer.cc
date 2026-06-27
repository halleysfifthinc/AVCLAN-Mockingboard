// copyright (C) 2006 Marcin Slonicki <marcin@softservice.com.pl>
// copyright (C) 2007 Louis Frigon
// Copyright (C) 2015 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cctype>
#include <cstdint>
#include <cstring>

#include "avclandrv.h"
#include "board.h"
#include "cdchanger.hpp"
#include "com232.h"
#include "peripheral.hpp"
#include "queue.hpp"

const char *const offon[] = {"OFF", "ON"};

constexpr uint8_t CACHE_SIZE = 32;

namespace {
AVCLAN_frame_t frames[CACHE_SIZE];

constinit Queue cache(frames);
constinit Queue incoming = cache;
constinit Queue outgoing = cache;

void toggle_flag(bool *flag, const char *msg) {
  *flag = !*flag;
  RS232_Print(msg);
  RS232_Print(offon[*flag]);
  RS232_Print("\n");
}

void set_flag(bool *flag, bool val, const char *msg) {
  *flag = val;
  RS232_Print(msg);
  RS232_Print(offon[val]);
  RS232_Print("\n");
}

void Setup();
void print_help();
} // namespace

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

  // Temporary, direct access is questionable since cache has ownership
  for (auto &frame : frames) {
    frame.control = 0x0f;
  }

  avclan::Bus phy;
  avclan::Peripheral<avclan::CDChanger> cd_changer(phy, 0x360);
  using Error = decltype(cd_changer)::Error;

  Setup();
  print_help();

  while (true) {
    if (AVCLAN_busActive()) {
      if (auto msg = cache.pop()) {
        auto err = cd_changer.read(msg.get(), (log_t){.print = printAllFrames,
                                                      .binary = printBinary,
                                                      .verbose = verbose});
        if (err == Error::Read{0x00})
          incoming.push(std::move(msg));
      } else {
        RS232_Print("!! Dropping an incoming message; cache is empty !!\n");
      }
    }

    if (const auto *in = incoming.peek()) {
      if (auto out = cache.pop()) {
        cd_changer.route(in, out.get());
        incoming.pop();

        if (out->reaction > 0)
          outgoing.push(std::move(out));
      } else {
        RS232_Print("!! Unable to respond; cache is empty !!\n");
      }
    }

    cd_changer.poll_devices([&](auto dev) {
      if (auto status = cache.pop()) {
        dev.emit(status.get());
        outgoing.push(std::move(status));
        return true;
      }
      return false;
    });

    if (auto out = outgoing.pop()) {
      auto err = cd_changer.send(
          out.get(), (log_t){.print = printAllFrames, .binary = printBinary});
      cd_changer.react(out.get(), err);
      if (out->reaction > 0)
        outgoing.push(std::move(out));
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
          if (auto out = cache.pop()) {
            out->is_unicast = true;
            out->controller_addr = cd_changer.address();
            out->peripheral_addr = HU_ADDR;
            {
              const uint8_t beep[] = {0x00, dev_CD_CHANGER, dev_BEEP_SPEAKERS,
                                      0x60, 0x01};
              out->length = sizeof(beep);
              memcpy(out->data, beep, sizeof(beep));
            }
            out->reaction = 1;
            outgoing.push(std::move(out));
          }
          break;
        case 'P':
          if (auto out = cache.pop()) {
            out->is_unicast = true;
            out->controller_addr = cd_changer.address();
            out->peripheral_addr = HU_ADDR;
            {
              const uint8_t play[] = {0x00,     dev_COMM_CTRL,  dev_COMM_v1,
                                      Ejection, dev_CD_CHANGER, 0x01};
              out->length = sizeof(play);
              memcpy(out->data, play, sizeof(play));
            }
            out->reaction = avclan::CDChanger::reaction_t::r_Ejection;
            outgoing.push(std::move(out));
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
                if (auto out = cache.pop()) {
                  if (!AVCLAN_parseframe(data_tmp, --seqIdx, out.get())) {
                    out->reaction = 1;
                    outgoing.push(std::move(out));
                  }
                }
                readSeq = readBinary = false;
              } else
                goto DEFAULT; // reading binary and this is a real data byte;
                              // fall through to default
            } else {
              if (auto out = cache.pop()) {
                out->is_unicast = seqIsUnicast;
                out->controller_addr = cd_changer.address();
                out->peripheral_addr = seqIsUnicast ? HU_ADDR : 0x1FF;
                out->length = seqIdx;
                memcpy(out->data, data_tmp, seqIdx);
                out->reaction = 1;
                outgoing.push(std::move(out));
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

namespace {
void Setup() {
  board_init(); // clock + GPIO bring-up (target-specific)
  RS232_Init();
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

} // namespace
