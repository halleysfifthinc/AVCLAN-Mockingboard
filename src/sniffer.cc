// copyright (C) 2006 Marcin Slonicki <marcin@softservice.com.pl>
// copyright (C) 2007 Louis Frigon
// Copyright (C) 2015 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <utility>

#include "FreeRTOS.h" // IWYU pragma: export
#include "task.h"

#include "avclan.h"
#if AVCLAN_BLUETOOTH
  #include "bluetooth/mobile.hpp"
#endif
#include "devices/amplifier.hpp"
#include "devices/cdchanger.hpp"
#include "frame.hpp"
#include "hal/board.h"
#include "hal/stdio.h"
#include "media/binding.hpp"
#include "media/playback.hpp"
#include "peripheral.hpp"
#include "queue.hpp"
#include "stdshim.hpp"

using namespace avclan;

namespace {
using Periph = Peripheral<CDChanger, Amplifier>;

const char *const offon[] = {"OFF", "ON"};

constexpr uint8_t CACHE_SIZE = AVCLAN_MSG_QUEUE_SIZE;
// A queue that can hold the entire Frame pool is never full, so the sender's
// blocking push of a follow-up frame onto its own queue can't deadlock
static_assert(CACHE_SIZE >= AVCLAN_FRAME_POOL_N,
              "CACHE_SIZE must be >= avclan::Frame allocator pool capacity");

struct Pipeline {
  Periph &peripheral;
  Queue<Frame> &incoming;
  Queue<Frame> &outgoing;
};

struct Context : Pipeline {
#if AVCLAN_BLUETOOTH
  bluetooth::Mobile &mobile;
#endif
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
      *static_cast<Pipeline *>(pvParameters);
  while (true) {
    if (auto msg = peripheral.read(Print{
            .print = printAllFrames,
            .binary = printBinary,
            .verbose = verbose,
        }))
      incoming.push(std::move(*msg));
  }
}

[[noreturn]] void vRoutingTask(void *pvParameters) {
  auto &[peripheral, incoming, outgoing] =
      *static_cast<Pipeline *>(pvParameters);
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
      *static_cast<Pipeline *>(pvParameters);
  while (true) {
    if (auto msg = peripheral.poll())
      outgoing.push(std::move(msg));
  }
}

[[noreturn]] void vSenderTask(void *pvParameters) {
  using Print = Frame::Print;
  auto &[peripheral, incoming, outgoing] =
      *static_cast<Pipeline *>(pvParameters);
  while (true) {
    auto result = peripheral.send(
        outgoing.pop(), Print{.print = printAllFrames, .binary = printBinary});
    if (auto next = peripheral.react(std::move(result)))
      outgoing.push(std::move(next));
  }
}

void begin_sequence(bool unicast) {
  puts(unicast ? "READ SEQUENCE (U)> " : "READ SEQUENCE (B)> ");
  lastPrintAllFrames = printAllFrames;
  printAllFrames = false;
  readSeq = true;
  seqIdx = hexDigit = 0;
  hexChars[0] = hexChars[1] = 0;
  seqIsUnicast = unicast;
}

struct Command {
  char key;
  const char *help;
  void (*run)(Context &);
};

constexpr Command commands[] = {
    {
        .key = 'U',
        .help = "begin reading for unicast message (send with Enter key)",
        .run = [](Context &) static { begin_sequence(true); },
    },
    {
        .key = 'B',
        .help = "begin reading for broadcast message (send with Enter key)",
        .run = [](Context &) static { begin_sequence(false); },
    },
    {
        .key = 'm',
        .help = "Toggle mute for mockingboard bus activity",
        .run =
            [](Context &ctx) static {
              toggle_flag(&muteBus, "Mute device:");
              ctx.peripheral.mute(muteBus);
            },
    },
    {
        .key = 'v',
        .help = "Toggle verbose error logging",
        .run =
            [](Context &) static { toggle_flag(&verbose, "Verbose errors:"); },
    },
    {
        .key = 'l',
        .help = "Toggle message logging",
        .run =
            [](Context &) static { toggle_flag(&printAllFrames, "Logging:"); },
    },

    // X/x isn't a toggle interface because this is used programmatically and
    // is simpler than reading back the toggle state
    {
        .key = 'X',
        .help = "Turn binary logging ON",
        .run =
            [](Context &) static { set_flag(&printBinary, true, "Binary:"); },
    },
    {
        .key = 'x',
        .help = "Turn binary logging OFF",
        .run =
            [](Context &) static { set_flag(&printBinary, false, "Binary:"); },
    },

    {
        .key = 'k',
        .help = "Toggle character echo",
        .run =
            [](Context &) static {
              toggle_flag(&echoCharacters, "Echo characters:");
            },
    },
    {
        .key = 'E',
        .help = "Beep",
        .run =
            [](Context &ctx) static {
              using enum Action;
              using enum Device;
              if (auto out = Frame::acquire()) {
                out->is_unicast = true;
                out->peripheral_addr = ctx.peripheral.controller();
                {
                  const uint8_t beep[] = {
                      0x00,
                      to_underlying(CD_CHANGER),
                      to_underlying(BEEP_SPEAKERS),
                      0x60,
                      0x01,
                  };
                  out->length = sizeof(beep);
                  memcpy(out->data, beep, sizeof(beep));
                }
                out->reaction = 1;
                ctx.outgoing.push(std::move(out));
              } else
                puts("!! failed Frame alloc for Beep !! ");
            },
    },
    {
        .key = 'P',
        .help = "Play",
        .run =
            [](Context &ctx) static {
              using enum Action;
              using enum Device;
              if (auto out = Frame::acquire()) {
                out->is_unicast = true;
                out->peripheral_addr = ctx.peripheral.controller();
                {
                  const uint8_t play[] = {
                      0x00,
                      to_underlying(COMM_CTRL),
                      to_underlying(COMMUNICATION_V1),
                      to_underlying(Ejection),
                      to_underlying(CD_CHANGER),
                      0x01,
                  };
                  out->length = sizeof(play);
                  memcpy(out->data, play, sizeof(play));
                }
                out->reaction = CDChanger::reaction_t::r_Ejection;
                ctx.outgoing.push(std::move(out));
              } else
                puts("!! failed Frame alloc for Play !! ");
            },
    },

#ifndef NDEBUG
  #if AVCLAN_BLUETOOTH
    {
        .key = 'F',
        .help = "Forget all Bluetooth pairings",
        .run = [](Context &ctx) static { ctx.mobile.forget_pairings(); },
    },
  #endif
  #ifdef MEASURE_BUS
    {'M', "Measure bit-timing (pulse-widths and periods)",
     [](Context &ctx) static { ctx.peripheral.get_bus().measure(); }},
  #endif
    {
        .key = '+',
        .help = "Set bus driven/dominant",
        .run =
            [](Context &ctx) static {
              ctx.peripheral.get_bus().deafen(true);
              ctx.peripheral.get_bus().set_dominant();
              puts("Set bus dominant...");
            },
    },
    {
        .key = '-',
        .help = "Set bus idle/recessive",
        .run =
            [](Context &ctx) static {
              ctx.peripheral.get_bus().set_recessive();
              ctx.peripheral.get_bus().deafen(false);
              puts("Set bus recessive...");
            },
    },
#endif

    {
        .key = '?',
        .help = "Print this message",
        .run = [](Context &) static { print_help(); },
    },
};

static_assert(
    [] {
      for (size_t i = 0; i < std::size(commands); i++)
        for (size_t j = i + 1; j < std::size(commands); j++)
          if (commands[i].key == commands[j].key)
            return false;
      return true;
    }(),
    "REPL command keys must be unique");
static_assert(
    [] {
      for (const auto &cmd : commands)
        if (cmd.key == 0x10 || cmd.key == '\r' || cmd.key == '\n')
          return false;
      return true;
    }(),
    "REPL command keys 0x10, CR and LF are reserved for sequence entry");

[[noreturn]] void vREPLTask(void *pvParameters) {
  auto &ctx = *static_cast<Context *>(pvParameters);
  auto &[peripheral, incoming, outgoing] = static_cast<Pipeline &>(ctx);
  while (true) {
    const int readkey = fgetc(stdin);
    if (readkey == EOF) {
      clearerr(stdin);
      continue;
    }

    if (!readSeq) {
      if (readkey == 0x10) { // Signals binary sequence incoming
        readSeq = readBinary = true;
        seqIdx = 0;
      } else {
        for (const auto &cmd : commands) {
          if (cmd.key == readkey) {
            cmd.run(ctx);
            break;
          }
        }
      }
      continue;
    }

    switch (readkey) {
      case '\r': // Eat carriage return too
        if (readBinary)
          goto DEFAULT;
        [[fallthrough]];
      case '\n':
        if (!readBinary && seqIdx == 0) {
          // Nothing to send, so leave the mode instead: an escape for an
          // entry started by accident.
          readSeq = false;
          hexDigit = hexChars[0] = hexChars[1] = 0;
          printAllFrames = lastPrintAllFrames;
          break;
        }
        if (seqIdx > 0) {
          if (readBinary) {
            if (data_tmp[seqIdx - 1] == 0x17) {
              if (auto out = Frame::acquire()) {
                if (out->parse(data_tmp, --seqIdx) == Frame::Error::Parse{0}) {
                  out->reaction = 1;
                  outgoing.push(std::move(out));
                  readSeq = readBinary = false;
                }
              } else
                puts("!! failed Frame alloc for input message !!");
              break;
            }
            // Otherwise a real data byte
          } else { // ASCII message
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
            break;
          }
        }
        [[fallthrough]];
      DEFAULT:
      default:
        // Binary mode carries the full wire preamble; hex mode is payload
        // only, so it stops one past MAXLENGTH to let '\n' report overflow.
        if (seqIdx >=
            (readBinary ? sizeof(data_tmp) : uint8_t{Frame::MAXLENGTH + 1})) {
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
    } // switch (readkey)
  }
}

} // namespace

int main() {
  Setup();
  print_help();

  // Static: the scheduler reclaims main's stack for ISRs
  static Bus phy;
  static Periph peripheral(phy, 0x360);
#ifndef NDEBUG
  peripheral.set_controller(0x190);
#endif
#if AVCLAN_BLUETOOTH
  static bluetooth::Mobile mobile;
  static media::Binding<media::Playback> playback;
  static media::Binding<media::Volume> volume;
  peripheral.device<CDChanger>().attach(playback);
  peripheral.device<Amplifier>().attach(volume);
  mobile.attach(playback);
  mobile.attach(volume);
#endif
  static Queue<Frame> incoming(CACHE_SIZE);
  static Queue<Frame> outgoing(CACHE_SIZE);
  static Pipeline pipeline{
      .peripheral = peripheral,
      .incoming = incoming,
      .outgoing = outgoing,
  };
  static Context ctx{
      pipeline,
#if AVCLAN_BLUETOOTH
      mobile,
#endif
  };

  xTaskCreateAffinitySet(vSenderTask, "send/react", configMINIMAL_STACK_SIZE,
                         &pipeline, SendPriority, 0b01, nullptr);
  xTaskCreateAffinitySet(vRoutingTask, "router", configMINIMAL_STACK_SIZE,
                         &pipeline, RoutePriority, 0b01, nullptr);
  xTaskCreateAffinitySet(vPollTask, "poll", configMINIMAL_STACK_SIZE, &pipeline,
                         PollPriority, 0b01, nullptr);
  xTaskCreateAffinitySet(vReceiverTask, "receiver", configMINIMAL_STACK_SIZE,
                         &pipeline, ReceivePriority, 0b01, nullptr);
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
  for (const auto &cmd : commands)
    printf("%c - %s\n", cmd.key, cmd.help);
}

} // namespace
