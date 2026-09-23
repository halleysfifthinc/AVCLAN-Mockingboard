// copyright (C) 2006 Marcin Slonicki <marcin@softservice.com.pl>
// copyright (C) 2007 Louis Frigon
// Copyright (C) 2015 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>

#include "FreeRTOS.h" // IWYU pragma: export
#include "task.h"

#include "cdchanger.hpp"
#include "frame.hpp"
#include "hal/board.h"
#include "hal/stdio.h"
#include "peripheral.hpp"
#include "queue.hpp"
#include "stdshim.hpp"

using namespace avclan;

namespace {
using Periph = Peripheral<CDChanger>;

const char *const offon[] = {"OFF", "ON"};

constexpr uint8_t CACHE_SIZE = AVCLAN_MSG_QUEUE_SIZE;
// A queue that can hold the entire Frame pool is never full, so the sender's
// blocking push of a follow-up frame onto its own queue can't deadlock
static_assert(CACHE_SIZE >= AVCLAN_FRAME_POOL_N,
              "CACHE_SIZE must be >= avclan::Frame allocator pool capacity");

// Must be ranked such that queues deterministically tend to empty
constexpr UBaseType_t SendPriority = tskIDLE_PRIORITY + 3;
constexpr UBaseType_t RoutePriority = tskIDLE_PRIORITY + 2;
constexpr UBaseType_t PollPriority = tskIDLE_PRIORITY + 2;
constexpr UBaseType_t ReceivePriority = tskIDLE_PRIORITY + 1;

struct Context {
  Periph &peripheral;
  Queue<Frame> &incoming;
  Queue<Frame> &outgoing;
};

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

[[noreturn]] void vReceiverTask(void *pvParameters) {
  using Print = Frame::Print;
  auto &[peripheral, incoming, outgoing] =
      *static_cast<Context *>(pvParameters);
  while (true) {
    if (auto msg = peripheral.read(Print{.print = printAllFrames,
                                         .binary = printBinary,
                                         .verbose = verbose}))
      incoming.push(std::move(*msg));
  }
}

[[noreturn]] void vRoutingTask(void *pvParameters) {
  auto &[peripheral, incoming, outgoing] =
      *static_cast<Context *>(pvParameters);
  while (true) {
    const Frame *in = incoming.peek();
    if (auto resp = peripheral.route(*in)) {
      incoming.pop();
      if (*resp)
        outgoing.push(std::move(*resp));
    }
  }
}

[[noreturn]] void vPollTask(void *pvParameters) {
  auto &[peripheral, incoming, outgoing] =
      *static_cast<Context *>(pvParameters);
  while (true) {
    if (auto msg = peripheral.poll())
      outgoing.push(std::move(msg));
  }
}

[[noreturn]] void vSenderTask(void *pvParameters) {
  using Print = Frame::Print;
  auto &[peripheral, incoming, outgoing] =
      *static_cast<Context *>(pvParameters);
  while (true) {
    auto result = peripheral.send(
        outgoing.pop(), Print{.print = printAllFrames, .binary = printBinary});
    if (auto next = peripheral.react(std::move(result)))
      outgoing.push(std::move(next));
  }
}

[[noreturn]] void vREPLTask(void *pvParameters) {
  using enum Action;
  using enum Device;
  auto &[peripheral, incoming, outgoing] =
      *static_cast<Context *>(pvParameters);
  while (true) {
    if (int readkey = fgetc(stdin); readkey != EOF) {
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
          if (auto out = Frame::acquire()) {
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
          if (auto out = Frame::acquire()) {
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
        case '+':
          peripheral.get_bus().deafen(true);
          peripheral.get_bus().set_dominant();
          puts("Set bus dominant...");
          break;
        case '-':
          peripheral.get_bus().set_recessive();
          peripheral.get_bus().deafen(false);
          puts("Set bus recessive...");
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
        case '\r': // Eat carriage return too
          if (readBinary)
            goto DEFAULT;
          [[fallthrough]];
        case '\n':
          if (readSeq && !readBinary && seqIdx == 0) {
            // Nothing to send, so leave the mode instead: an escape for an
            // entry started by accident.
            readSeq = false;
            hexDigit = hexChars[0] = hexChars[1] = 0;
            printAllFrames = lastPrintAllFrames;
            break;
          }
          if (readSeq && seqIdx > 0) {
            if (readBinary) {
              if (data_tmp[seqIdx - 1] == 0x17) {
                if (auto out = Frame::acquire()) {
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
              if (auto out = Frame::acquire()) {
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
              // Only take valid hex digits
              if (isxdigit(readkey) == 0)
                break;

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
    } else {
      clearerr(stdin);
    } // if (readkey != EOF)
  }
}

} // namespace

int main() {
  Setup();
  print_help();

  // Static: the scheduler reclaims main's stack for ISRs
  static Bus phy;
  static Periph peripheral(phy, 0x360);
  static Queue<Frame> incoming(CACHE_SIZE);
  static Queue<Frame> outgoing(CACHE_SIZE);
  static Context ctx{
      .peripheral = peripheral, .incoming = incoming, .outgoing = outgoing};

  xTaskCreateAffinitySet(vSenderTask, "send/react", configMINIMAL_STACK_SIZE,
                         &ctx, SendPriority, 0b01, nullptr);
  xTaskCreateAffinitySet(vRoutingTask, "router", configMINIMAL_STACK_SIZE, &ctx,
                         RoutePriority, 0b01, nullptr);
  xTaskCreateAffinitySet(vPollTask, "poll", configMINIMAL_STACK_SIZE, &ctx,
                         PollPriority, 0b01, nullptr);
  xTaskCreateAffinitySet(vReceiverTask, "receiver", configMINIMAL_STACK_SIZE,
                         &ctx, ReceivePriority, 0b01, nullptr);
  xTaskCreateAffinitySet(vREPLTask, "repl", configMINIMAL_STACK_SIZE, &ctx,
                         tskIDLE_PRIORITY, 0b01, nullptr);

  vTaskStartScheduler();
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
       "+ - Set bus driven/dominant\n"
       "- - Set bus idle/recessive\n"
#endif
       "? - Print this message");
}

} // namespace
