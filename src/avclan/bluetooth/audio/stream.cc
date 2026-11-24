// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cinttypes>
#include <concepts>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <variant>

#include "FreeRTOS.h" // IWYU pragma: export
#include "task.h"

#include "avclan.h"
#include "stream.hpp"

namespace avclan::bluetooth::audio {

namespace {
// With BTstack, off the bus core
constexpr UBaseType_t STREAM_CORES = 0b10;
constexpr configSTACK_DEPTH_TYPE STACK_WORDS = 1024;
} // namespace

Stream::Stream() {
  for (Payload &payload : payloads)
    free_payloads.send(&payload);
  output::init();
  xTaskCreateAffinitySet([](void *self) { static_cast<Stream *>(self)->run(); },
                         "audio", STACK_WORDS, this, AudioPriority,
                         STREAM_CORES, &task);
}

void Stream::configure(const CodecConfig &cfg) {
  if (!messages.send(Configure{cfg}))
    drops.fetch_add(1, std::memory_order_relaxed);
}

void Stream::start() {
  if (!messages.send(Start{}))
    drops.fetch_add(1, std::memory_order_relaxed);
}

void Stream::write(std::span<const uint8_t> payload) {
  if (payload.size() <= MAX_PAYLOAD)
    if (const std::optional<Payload *> slot = free_payloads.try_receive()) {
      Payload *p = *slot;
      p->size = static_cast<uint16_t>(payload.size());
      std::ranges::copy(payload, p->bytes);
      if (messages.send(p))
        return;
      free_payloads.send(p);
    }
  drops.fetch_add(1, std::memory_order_relaxed);
}

void Stream::stop() {
  if (!messages.send(Stop{}))
    drops.fetch_add(1, std::memory_order_relaxed);
}

void Stream::close() {
  if (!messages.send(Close{}))
    drops.fetch_add(1, std::memory_order_relaxed);
}

void Stream::run() {
  while (true)
    std::visit(
        [this]<class M>(const M &message) {
          if constexpr (std::same_as<M, Configure>)
            set_decoder(message.config);
          else if constexpr (std::same_as<M, Start>)
            open();
          else if constexpr (std::same_as<M, Payload *>) {
            std::visit(
                [&]<class D>(D &dec) {
                  if constexpr (!std::same_as<D, std::monostate>)
                    if (!dec.decode({message->bytes, message->size}))
                      decode_errors.fetch_add(1, std::memory_order_relaxed);
                },
                decoder);
            free_payloads.send(message);
          } else if constexpr (std::same_as<M, Stop>)
            reset();
          else if constexpr (std::same_as<M, Close>)
            shut();
          else
            static_assert(false);
        },
        messages.receive());
}

// A new codec keeps the output clocked unless its rate changes
void Stream::set_decoder(const CodecConfig &cfg) {
  if (codec_rate() != std::visit([](const auto &c) { return c.rate; }, cfg))
    shut();

  const PcmOut sink{
      .fn =
          [](void *ctx, const int16_t *pcm, uint16_t frames, uint8_t channels) {
            static_cast<Stream *>(ctx)->push(pcm, frames, channels);
          },
      .ctx = this,
  };
  std::visit(
      [&]<class C>(const C &c) {
        if constexpr (std::same_as<C, SbcConfig>)
          decoder.emplace<SbcDecoder>(c, sink);
        else
          static_assert(false);
        resampler.reset(c.channels);
      },
      cfg);
}

void Stream::open() {
  using namespace std::chrono_literals;

  const std::optional<uint32_t> rate = codec_rate();
  if (!rate)
    return;

  uint32_t actual = out_rate.load(std::memory_order_relaxed);
  if (actual == 0) {
    actual = output::start(*rate, [this] { return next_block(); });
    out_rate.store(actual, std::memory_order_relaxed);
  }
  nominal = static_cast<uint32_t>((uint64_t{*rate} << 16) / actual);
  target = static_cast<size_t>(actual * TARGET / 1s);
  band = static_cast<size_t>(actual * BAND / 1s);

  reset();
  factor.store(resampler.set_factor(nominal), std::memory_order_relaxed);
}

// Drops what's buffered
void Stream::reset() {
  // From here the IRQ leaves `tail` alone
  playing.store(false, std::memory_order_relaxed);
  head.store(tail.load(std::memory_order_relaxed), std::memory_order_relaxed);
}

void Stream::shut() {
  if (out_rate.load(std::memory_order_relaxed) != 0) {
    reset();
    output::stop();
    out_rate.store(0, std::memory_order_relaxed);
  }
  decoder.emplace<std::monostate>();
}

std::optional<uint32_t> Stream::codec_rate() const {
  return std::visit(
      []<class D>(const D &dec) -> std::optional<uint32_t> {
        if constexpr (std::same_as<D, std::monostate>)
          return std::nullopt;
        else
          return dec.config().rate;
      },
      decoder);
}

void Stream::push(const int16_t *pcm, uint16_t frames, uint8_t channels) {
  if (channels != resampler.channels()) {
    decode_errors.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  size_t pos = head.load(std::memory_order_relaxed);
  resampler.process(pcm, frames, [&](const int16_t *out, uint16_t produced) {
    if (produced > RING_CAPACITY - level()) {
      drops.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    for (size_t i = 0; i < produced; i++) {
      const int16_t *frame = out + (i * channels);
      const int16_t left = frame[0];
      const int16_t right = frame[channels - 1]; // mono: left again
      ring[pos] = (StereoFrame{static_cast<uint16_t>(left)} << 16) |
                  static_cast<uint16_t>(right);
      if (++pos == RING_FRAMES)
        pos = 0;
    }
    head.store(pos, std::memory_order_release);
  });

  const size_t queued = level();
  uint32_t step = nominal;
  if (queued + band < target)
    step -= CORRECTION;
  else if (queued > target + band)
    step += CORRECTION;
  factor.store(resampler.set_factor(step), std::memory_order_relaxed);

  if (!playing.load(std::memory_order_relaxed) && queued >= target)
    playing.store(true, std::memory_order_release);
}

std::optional<output::Block> Stream::next_block() {
  if (!playing.load(std::memory_order_acquire))
    return std::nullopt;
  if (level() < BLOCK_FRAMES) {
    playing.store(false, std::memory_order_relaxed);
    underruns.fetch_add(1, std::memory_order_relaxed);
    return std::nullopt;
  }
  const size_t first = tail.load(std::memory_order_relaxed);
  tail.store((first + BLOCK_FRAMES) % RING_FRAMES, std::memory_order_release);
  // `tail` moves by whole blocks, so no block wraps
  static_assert(RING_FRAMES % BLOCK_FRAMES == 0);
  return std::span{ring}.subspan(first).first<BLOCK_FRAMES>();
}

size_t Stream::level() const {
  return (head.load(std::memory_order_acquire) + RING_FRAMES -
          tail.load(std::memory_order_acquire)) %
         RING_FRAMES;
}

#ifndef NDEBUG
void Stream::print_stats() const {
  if (const uint32_t rate = out_rate.load(std::memory_order_relaxed); rate == 0)
    puts("audio: closed");
  else
    printf("audio: %" PRIu32 " Hz, %s, level %" PRIu32
           " ms, factor 0x%05" PRIx32 "\n",
           rate,
           playing.load(std::memory_order_relaxed) ? "playing" : "buffering",
           static_cast<uint32_t>(level() * 1000 / rate),
           factor.load(std::memory_order_relaxed));
  printf("audio: drops %" PRIu32 ", underruns %" PRIu32
         ", decode errors %" PRIu32 ", stack %u words free\n",
         drops.load(std::memory_order_relaxed),
         underruns.load(std::memory_order_relaxed),
         decode_errors.load(std::memory_order_relaxed),
         static_cast<unsigned>(uxTaskGetStackHighWaterMark(task)));
}
#endif

} // namespace avclan::bluetooth::audio
