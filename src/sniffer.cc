// copyright (C) 2006 Marcin Slonicki <marcin@softservice.com.pl>
// copyright (C) 2007 Louis Frigon
// Copyright (C) 2015 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <utility>

#include "cdchanger.hpp"
#include "frame.hpp"
#include "hal/board.h"
#include "hal/stdio.h"
#include "peripheral.hpp"
#include "queue.hpp"
#include "stdshim.hpp"

const char *const offon[] = {"OFF", "ON"};

constexpr uint8_t CACHE_SIZE = 16;
static_assert(CACHE_SIZE >= AVCLAN_FRAME_POOL_N,
              "CACHE_SIZE must be >= avclan::Frame allocator pool capacity");

using namespace avclan;

namespace {
constinit Queue<Frame, CACHE_SIZE> incoming;
constinit Queue<Frame, CACHE_SIZE> outgoing;

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
  using Print = Frame::Print;

  Setup();
  print_help();

  while (true) {
    if (peripheral.bus_is_active()) {
      if (auto msg = peripheral.read(Print{.print = printAllFrames,
                                           .binary = printBinary,
                                           .verbose = verbose}))
        incoming.push(std::move(*msg));
    }

    if (const auto *in = incoming.peek()) {
      if (auto resp = peripheral.route(*in)) {
        incoming.pop();
        if (*resp) {
          outgoing.push(std::move(*resp));
          continue; // route can be long; re-check the bus before poll/send
        }
      }
    }

    if (auto msg = peripheral.poll())
      outgoing.push(std::move(msg));

    if (auto out = outgoing.pop()) {
      auto result =
          peripheral.send(std::move(out), Print{.print = printAllFrames,
                                                .binary = printBinary});
      if (auto next = peripheral.react(std::move(result)))
        outgoing.push(std::move(next));

      continue;
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
          if (auto out = std::unique_ptr<Frame>(new (std::nothrow) Frame)) {
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
            puts("!! failed Frame alloc for Beep !! ");
          break;
        case 'P':
          if (auto out = std::unique_ptr<Frame>(new (std::nothrow) Frame)) {
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
          } else
            puts("!! failed Frame alloc for Play !! ");
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
  #ifdef MEASURE_BUS
        case 'M': peripheral.get_bus().measure(); break;
  #endif
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
                if (auto out =
                        std::unique_ptr<Frame>(new (std::nothrow) Frame)) {
                  if (out->parse(data_tmp, --seqIdx) ==
                      Frame::Error::Parse{0}) {
                    out->reaction = 1;
                    outgoing.push(std::move(out));
                    readSeq = readBinary = false;
                  }
                } else
                  puts("!! failed Frame alloc for input message !!");
              } else
                goto DEFAULT; // reading binary and this is a real data byte;
                              // fall through to default
            } else {          // ASCII message
              if (auto out = std::unique_ptr<Frame>(new (std::nothrow) Frame)) {
                const uint8_t sendLen =
                    seqIdx <= Frame::MAXLENGTH ? seqIdx : Frame::MAXLENGTH;
                out->is_unicast = seqIsUnicast;
                out->peripheral_addr =
                    seqIsUnicast ? peripheral.controller() : 0x1FF;
                out->length = sendLen;
                memcpy(out->data, data_tmp, sendLen);
                out->reaction = 1;
                outgoing.push(std::move(out));

                if (seqIdx > Frame::MAXLENGTH)
                  printf("!! sequence too long (%u > %u), truncated !!\n",
                         static_cast<unsigned>(seqIdx),
                         static_cast<unsigned>(Frame::MAXLENGTH));

                // Only leave hex-entry mode and restore logging once the
                // message actually sent, so a failed alloc can be retried
                // with '\n' instead of silently dropping the entry.
                readSeq = false;
                seqIdx = hexDigit = 0;
                printAllFrames = lastPrintAllFrames;
              } else
                puts("!! failed Frame alloc for input message !!");
            }
            break;
          }
        DEFAULT:
        default:
          if (readSeq) {
            // Binary mode carries the full wire preamble; hex mode is payload
            // only, so it stops one past MAXLENGTH to let '\n' report overflow.
            if (seqIdx >= (readBinary ? sizeof(data_tmp)
                                      : uint8_t{Frame::MAXLENGTH + 1})) {
              puts("!! sequence buffer full, ignoring further input !!");
              break;
            }

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
  puts("U - begin reading for unicast message (send with Enter key)\n"
       "B - begin reading for broadcast message (send with Enter key)\n"
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
