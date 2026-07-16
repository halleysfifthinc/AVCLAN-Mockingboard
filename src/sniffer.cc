// copyright (C) 2006 Marcin Slonicki <marcin@softservice.com.pl>
// copyright (C) 2007 Louis Frigon
// Copyright (C) 2015 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "cdchanger.hpp"
#include "frame.hpp"
#include "hal/board.h"
#include "hal/stdio.h"
#include "peripheral.hpp"
#include "queue.hpp"

const char *const offon[] = {"OFF", "ON"};

constexpr uint8_t CACHE_SIZE = 32;

using namespace avclan;

namespace {
Frame frames[CACHE_SIZE];

constinit Queue cache(frames);
constinit Queue incoming = cache;
constinit Queue outgoing = cache;

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
uint8_t data_tmp[Frame::MAXLENGTH + sizeof(Frame)];
uint8_t seqIdx = 0; // current index in data_tmp

void toggle_flag(bool *flag, const char *msg) {
  *flag = !*flag;
  printf("%s %s\n", msg, offon[*flag]);
}

void set_flag(bool *flag, bool val, const char *msg) {
  *flag = val;
  printf("%s %s\n", msg, offon[val]);
}

void Setup();
void print_help();
} // namespace

int main() {
  Bus phy;
  using enum Action;
  using enum Device;
  Peripheral<CDChanger> peripheral(phy, 0x360);
  using Error = decltype(peripheral)::Error;
  using Print = Frame::Print;

  Setup();
  print_help();

  while (true) {
    if (peripheral.bus_is_active()) {
      if (auto msg = cache.pop()) {
        auto err = peripheral.read(msg.get(), Print{.print = printAllFrames,
                                                    .binary = printBinary,
                                                    .verbose = verbose});
        if (err == Error::Read{0x00})
          incoming.push(std::move(msg));
      } else {
        puts("!! Dropping an incoming message; cache is empty !!");
      }
    }

    if (const auto *in = incoming.peek()) {
      if (auto out = cache.pop()) {
        peripheral.route(in, out.get());
        incoming.pop();

        if (out->reaction > 0)
          outgoing.push(std::move(out));
      } else {
        puts("!! Unable to respond; cache is empty !!");
      }
    }

    if (peripheral.pending()) {
      if (auto out = cache.pop(); out && peripheral.emit(out.get()))
        outgoing.push(std::move(out));
    }

    if (auto out = outgoing.pop()) {
      auto err = peripheral.send(
          out.get(), Print{.print = printAllFrames, .binary = printBinary});
      peripheral.react(out.get(), err);
      if (out->reaction > 0)
        outgoing.push(std::move(out));
    }

    // stdin must be non-blocking: yielding EOF when idle/empty
    if (int readkey = getchar(); readkey != EOF) {
      switch (readkey) {
        case '?': print_help(); break;
        case 'v': toggle_flag(&verbose, "Verbose errors:"); break;
        case 'l': toggle_flag(&printAllFrames, "Logging:"); break;
        case 'k': toggle_flag(&echoCharacters, "Echo characters:"); break;
        case 'm':
          toggle_flag(&muteBus, "Mute device:");
          peripheral.mute(muteBus);
          break;

        // X/x isn't a toggle interface because this is used
        // programmatically and is simpler than reading back the toggle
        // state
        case 'X': set_flag(&printBinary, true, "Binary:"); break;
        case 'x': set_flag(&printBinary, false, "Binary:"); break;

        case 'E': // Beep
          if (auto out = cache.pop()) {
            out->is_unicast = true;
            out->peripheral_addr = peripheral.controller();
            {
              const uint8_t beep[] = {0x00, to_underlying(CD_CHANGER),
                                      to_underlying(BEEP_SPEAKERS), 0x60, 0x01};
              out->length = sizeof(beep);
              memcpy(out->data, beep, sizeof(beep));
            }
            out->reaction = 1;
            outgoing.push(std::move(out));
          } else
            puts("!! Cache empty; unable to queue beep request");
          break;
        case 'P':
          if (auto out = cache.pop()) {
            out->is_unicast = true;
            out->peripheral_addr = peripheral.controller();
            {
              const uint8_t play[] = {0x00,
                                      to_underlying(COMM_CTRL),
                                      to_underlying(COMMUNICATION_V1),
                                      to_underlying(Ejection),
                                      to_underlying(CD_CHANGER),
                                      0x01};
              out->length = sizeof(play);
              memcpy(out->data, play, sizeof(play));
            }
            out->reaction = CDChanger::reaction_t::r_Ejection;
            outgoing.push(std::move(out));
          }
          break;

#ifndef NDEBUG
        case 'g': peripheral.device<CDChanger>().mic_toggle(); break;
        case 'p':
          fputs("First play/pause begin ... ", stdout);
          peripheral.device<CDChanger>().media_action(MediaAction::Play_Pause);
          while (peripheral.device<CDChanger>().media_busy()) {}
          fputs("end\nSecond play/pause begin ... ", stdout);
          peripheral.device<CDChanger>().media_action(MediaAction::Play_Pause);
          while (peripheral.device<CDChanger>().media_busy()) {}
          puts("end");
          break;
        case 's':
          fputs("Skip begin ... ", stdout);
          peripheral.device<CDChanger>().media_action(MediaAction::Track_Next);
          while (peripheral.device<CDChanger>().media_busy()) {}
          puts("end");
          break;
        case 'b':
          fputs("Skip back begin ... ", stdout);
          peripheral.device<CDChanger>().media_action(MediaAction::Track_Prev);
          while (peripheral.device<CDChanger>().media_busy()) {}
          puts("end");
          break;
        case 'M': peripheral.get_bus().measure(); break;
#endif

        case 0x10: // Signals binary sequence incoming
          if (!readSeq && !readBinary) {
            readSeq = readBinary = true;
            seqIdx = 0;
            break;
          } else
            goto DEFAULT; // reading binary and this is a real data byte

        case 'U': // Send command
          puts("READ SEQUENCE (U)> ");
          lastPrintAllFrames = printAllFrames;
          printAllFrames = false;
          readSeq = true;
          seqIdx = hexDigit = 0;
          hexChars[0] = hexChars[1] = 0;
          seqIsUnicast = true;
          break;
        case 'B': // Send broadcast
          puts("READ SEQUENCE (B)> ");
          lastPrintAllFrames = printAllFrames;
          printAllFrames = false;
          readSeq = true;
          seqIdx = hexDigit = 0;
          hexChars[0] = hexChars[1] = 0;
          seqIsUnicast = false;
          break;
        case '\n':
          if (readSeq && seqIdx > 0) {
            if (readBinary) {
              if (data_tmp[seqIdx - 1] == 0x17) {
                if (auto out = cache.pop()) {
                  if (out->parse(data_tmp, --seqIdx) ==
                      Frame::Error::Parse{0}) {
                    out->reaction = 1;
                    outgoing.push(std::move(out));
                  }
                }
                readSeq = readBinary = false;
              } else
                goto DEFAULT; // reading binary and this is a real data byte;
                              // fall through to default
            } else if (seqIdx <= Frame::MAXLENGTH) {
              if (auto out = cache.pop()) {
                out->is_unicast = seqIsUnicast;
                out->peripheral_addr =
                    seqIsUnicast ? peripheral.controller() : 0x1FF;
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
          if (readSeq && seqIdx < (Frame::MAXLENGTH + sizeof(Frame))) {
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
                fputs("CURRENT SEQUENCE > ", stdout);
                for (uint8_t i = 0; i < seqIdx; i++) {
                  printf("%02X", static_cast<unsigned>(data_tmp[i]));
                  putchar(' ');
                }
                putchar('\n');
              }
            }
          }
      } // switch (readkey)
    } // if (readkey != EOF)
  }
  return 0;
}

namespace {
void Setup() {
  board_init(); // clock + GPIO bring-up (target-specific)
  stdio_init();
  board_enable_interrupts();
}

void print_help() {
  puts("AVCLAN Mockingboard v1");
  puts("U - begin reading for unicast message\n"
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
       "? - Print this message");
}

} // namespace
