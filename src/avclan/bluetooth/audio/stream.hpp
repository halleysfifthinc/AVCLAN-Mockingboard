// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>
#include <variant>

#include "FreeRTOS.h" // IWYU pragma: export
#include "queue.h"
#include "task.h"

#include "btstack.h" // IWYU pragma: export

#include "codec.hpp"
#include "output.h"
#include "resampler.hpp"
#include "sbc.hpp"

namespace avclan::bluetooth::audio {

// One codec stream, from media payloads to the output. The producer API is for
// BTstack's context and never blocks; decoding runs on the stream's own task.
//
// States: configure() sets the codec and close() drops it. start() moves to
// Buffering, which becomes Playing at the target level; without a codec it
// does nothing. An underrun goes back to Buffering. stop() drops what's
// buffered and goes back to Buffering. The output is clocked from start()
// until close() or a configure() at another rate, so a pause doesn't pop.
class Stream {
public:
  // Before the scheduler starts
  Stream();
  Stream(const Stream &) = delete;

  void configure(const CodecConfig &cfg);
  void start();
  // Dropped while every payload buffer is in use
  void write(std::span<const uint8_t> payload);
  void stop();
  void close();

#ifndef NDEBUG
  void print_stats() const;
#endif

private:
  template <class T> class Channel {
    // FreeRTOS copies items in and out bytewise
    static_assert(std::is_trivially_copyable_v<T>);

  public:
    explicit Channel(UBaseType_t length)
        : handle{xQueueCreate(length, sizeof(T))} {
      configASSERT(handle);
    }
    Channel(const Channel &) = delete;

    bool send(const T &item) { return xQueueSend(handle, &item, 0) == pdPASS; }

    T receive() {
      T item{};
      xQueueReceive(handle, &item, portMAX_DELAY);
      return item;
    }

    std::optional<T> try_receive() {
      T item{};
      if (xQueueReceive(handle, &item, 0) != pdPASS)
        return std::nullopt;
      return item;
    }

  private:
    QueueHandle_t handle;
  };

  using StereoFrame = output::StereoFrame;
  static constexpr size_t BLOCK_FRAMES = output::BLOCK_FRAMES;
  static constexpr size_t RING_FRAMES = 48 * BLOCK_FRAMES;
  static constexpr size_t RING_CAPACITY =
      RING_FRAMES - (output::HELD_BLOCKS * BLOCK_FRAMES);
  static constexpr std::chrono::milliseconds TARGET{150};
  static constexpr std::chrono::milliseconds BAND{25};
  static constexpr uint32_t UNITY = 0x10000;
  static constexpr uint32_t CORRECTION = 0x100;

  static constexpr size_t MAX_PAYLOAD = AVDTP_L2CAP_MTU;
  static constexpr UBaseType_t PAYLOADS = 8;
  // Payloads take at most PAYLOADS of the queue, so these are for control
  static constexpr UBaseType_t CONTROL_SLOTS = 8;

  struct Payload {
    uint16_t size;
    uint8_t bytes[MAX_PAYLOAD];
  };
  struct Configure {
    CodecConfig config;
  };
  struct Start {};
  struct Stop {};
  struct Close {};

  [[noreturn]] void run();
  void set_decoder(const CodecConfig &cfg);
  void open();
  void reset();
  void shut();
  std::optional<uint32_t> codec_rate() const;
  void push(const int16_t *pcm, uint16_t frames, uint8_t channels);
  std::optional<output::Block> next_block();
  size_t level() const;

  // A payload not in free_payloads belongs to the message carrying it
  Payload payloads[PAYLOADS];
  Channel<Payload *> free_payloads{PAYLOADS};
  Channel<std::variant<Configure, Start, Payload *, Stop, Close>> messages{
      PAYLOADS + CONTROL_SLOTS};

  // The stream task
  std::variant<std::monostate, SbcDecoder> decoder;
  Resampler resampler;
  uint32_t nominal = UNITY;
  size_t target = 0;
  size_t band = 0;

  // Shared with the output's IRQ, on the stream task's core
  StereoFrame ring[RING_FRAMES];
  std::atomic<size_t> head = 0; // written by the task
  std::atomic<size_t> tail = 0; // advanced by the IRQ while playing
  std::atomic<bool> playing = false;

  std::atomic<uint32_t> out_rate = 0; // 0 while closed
  std::atomic<uint32_t> factor = UNITY;
  std::atomic<uint32_t> drops = 0;
  std::atomic<uint32_t> underruns = 0;
  std::atomic<uint32_t> decode_errors = 0;

  TaskHandle_t task = nullptr;
};

} // namespace avclan::bluetooth::audio
