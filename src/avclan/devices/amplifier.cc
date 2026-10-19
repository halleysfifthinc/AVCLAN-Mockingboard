// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <utility>

#include "amplifier.hpp"
#include "avclan.h"
#include "device.hpp"
#include "frame.hpp"
#include "media/volume.hpp"

namespace {
using avclan::media::Volume;

// How long a step waits for the amp to report the requested volume. Typical
// response delay is ~20 ms.
constexpr TickType_t timeout_period = pdMS_TO_TICKS(250);

// The amp's volume byte at each step, quietest first: BCD dB
// attenuation, 0xFF for -Inf (msgdumps/volume-range-unique.pcap)
constexpr std::array<uint8_t, 48> attenuation = {
    0xFF, 0x67, 0x59, 0x55, 0x51, 0x48, 0x45, 0x43, 0x41,       //
    0x39, 0x38, 0x37, 0x36, 0x35, 0x34, 0x33, 0x32, 0x31, 0x30, //
    0x29, 0x28, 0x27, 0x26, 0x25, 0x23, 0x22, 0x21, 0x20,       //
    0x19, 0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11, 0x10, //
    0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00,
};
static_assert(attenuation.size() == Volume::MAX + 1);

// Assert descending order (ie quietest to loudest)
// Required to index by "level" where 0 is quietest
static_assert(std::ranges::is_sorted(attenuation, std::ranges::greater{}));

constexpr auto level_of(uint8_t volume) {
  return static_cast<uint8_t>(
      std::ranges::lower_bound(attenuation, volume, std::ranges::greater{}) -
      attenuation.begin());
}

struct offset16_t {
  int8_t value_;

  static constexpr std::optional<offset16_t> from(uint8_t val) noexcept {
    if (val > 0x1F)
      return std::nullopt;

    return std::bit_cast<offset16_t>(val);
  };

  explicit operator int8_t() const {
    return static_cast<int8_t>(value_ - 0x10);
  };
};
} // namespace

namespace avclan {

void Amplifier::init(NotifyQueues queues) {
  Notifier::init(queues);
  timeout_ = xTimerCreate("amp set", timeout_period, false, this,
                          [](TimerHandle_t timer) static {
                            static_cast<Amplifier *>(pvTimerGetTimerID(timer))
                                ->notify(condition, r_FailedVolume);
                          });
  configASSERT(timeout_);
}

void Amplifier::clear_pending() {
  requested_.reset();
  // Spares a wasted wakeup; a late timeout is stale in fill()
  xTimerStop(timeout_, 0);
}

// Steps to a held phone level, or else tells the phone the amp's
void Amplifier::sync() {
  if (target_)
    notify(condition, r_VolumeRequest);
  else
    send(Volume::Level{level_of(*volume_)});
}

std::unique_ptr<Frame>
Amplifier::react(expected<std::unique_ptr<Frame>, detail::SendError> exp) {
  const uint8_t set = exp ? (*exp)->reaction : exp.error().reaction;
  const std::lock_guard lock(mutex);

  // Send result is irrelevant
  if (!requested_ ||  // Coincidentally observed as resolved before we can react
      set != set_id_) // Newer step is already queued
    return nullptr;

  // Before arming: a deadline past the timer's expiry would make its timeout
  // read as stale
  deadline_ = xTaskGetTickCount() + timeout_period;
  // An unarmed timeout would leave the set pending forever
  if (!exp || xTimerReset(timeout_, 0) != pdPASS) {
    deadline_ = xTaskGetTickCount(); // Shift deadline to "past" (when observed
                                     // in `fill` in the future)
    notify(condition, r_FailedVolume);
  }

  return nullptr;
}

void Amplifier::observe(const Frame &in) {
  if (in.is_unicast || in.length < 10)
    return;

  const uint8_t *data = in.data;
  const auto from = static_cast<Device>(*data++);
  const auto to = static_cast<Device>(*data++);
  const auto action = static_cast<Action>(*data++);
  if (from != Device::AUDIO_AMP || to != Device::STATUS ||
      action != Action::Playback_Status)
    return;

  const std::lock_guard lock(mutex);

  // Set each report for simplicity. Con: allows confused behavior if multiple
  // devices report amp state
  amp_addr_ = in.controller_addr;

  const auto b1 = *data++;
  // Only observed for all-zero data reports (cause/reason unclear)
  if (b1 == 0)
    return;

  const bool was_muted = std::exchange(amplifier_muted_, (b1 & (1 << 5U)) != 0);
  const uint8_t volume = *data++;
  const std::optional<uint8_t> previous = std::exchange(volume_, volume);

  if (const uint8_t level = level_of(volume); requested_ == level) {
    // (Successful) pending resolution
    clear_pending();
    notify(condition, r_VolumeRequest);
  } else if (volume != previous) {
    // The first report, or the head unit changed it (e.g. its knob, or a
    // recall on a source switch)
    if (requested_)
      clear_pending();

    if (source_active_) {
      // The head unit wins over a phone level
      if (previous)
        target_.reset();
      sync();
    }
  }
  // The amp mutes for 0.5-1.5 s after a source switch, just when a held level
  // is due, so steps wait for it to unmute
  if (was_muted && !amplifier_muted_ && target_ && !requested_)
    notify(condition, r_VolumeRequest);

  // // Skip reading/parsing remaining fields due to current unuse
  // constexpr auto offset16_to_int8 = &offset16_t::operator int8_t;
  // balance_ = offset16_t::from(*data++).transform(offset16_to_int8);
  // fade_ = offset16_t::from(*data++).transform(offset16_to_int8);
  // bass_ = offset16_t::from(*data++).transform(offset16_to_int8);
  // mid_ = offset16_t::from(*data++).transform(offset16_to_int8);
  // treble_ = offset16_t::from(*data++).transform(offset16_to_int8);
}

void Amplifier::active_changed(Device function) {
  const bool is_source = function != NoDevice && function == source_;
  // A fill() between the store and the requested_ read would send a step that
  // this report then contradicts
  const std::lock_guard lock(mutex);
  if (is_source == source_active_)
    return;
  source_active_ = is_source;
  // Amplifier volume is only synchronized with the source when active
  if (source_active_ && volume_ && !requested_)
    sync();
}

void Amplifier::wake(const Volume::Request & /* msg */) {
  notify(condition, r_VolumeRequest);
}

void Amplifier::receive(const Volume::Set &s) {
  target_ = std::min(s.level, Volume::MAX);
}

void Amplifier::receive(const Volume::Reset & /* reset */) {
  target_.reset();
  if (source_active_ && volume_)
    send(Volume::Level{level_of(*volume_)});
}

void Amplifier::fill(Frame &out, condition_t /* tag */, uint32_t payload) {
  const bool failed = payload == r_FailedVolume;
  // Stale: the step landed, or the timeout/failure is an earlier step's
  if (failed && (!requested_ || !deadline_ ||
                 static_cast<int32_t>(xTaskGetTickCount() - *deadline_) < 0))
    return;
  if (failed) {
    clear_pending();
    // Abandoned, unless the amp muted (e.g. a source switch reported after the
    // activation) and so may have ignored it; the unmute retries
    if (!amplifier_muted_)
      target_.reset();
  }

  const bool drained = drain();
  // Don't try to set the volume when waiting for a previous set to resolve (or
  // if no prior amp report)
  if (requested_ || !volume_)
    return;
  const uint8_t level = level_of(*volume_);
  // Amplifier steps are coarser than Bluetooth, so (multiple) volume steps
  // may not produce a different amplifier volume
  if (target_ == level)
    target_.reset();

  // Held until `source_` is active, and the amp reports (so its address is
  // known) and unmutes
  if (target_ && source_active_ && !amplifier_muted_) {
    const bool louder = *target_ > level;
    out.is_unicast = true;
    out.peripheral_addr = *amp_addr_;

    const uint8_t step_volume[] = {
        0x00,
        to_underlying(Device::CMD_SW),
        to_underlying(Device::AUDIO_AMP),
        to_underlying(louder ? Action::Pwrvol_Knob_Righthand_Turn
                             : Action::Pwrvol_Knob_Lefthand_Turn),
        // The amp ignores the step size and moves one level per frame
        0x01,
    };
    out.length = sizeof(step_volume);
    memcpy(out.data, step_volume, sizeof(step_volume));
    // 1..255, as reaction 0 marks a non-sendable frame
    out.reaction = set_id_ = static_cast<uint8_t>((set_id_ % 255) + 1);
    requested_ = static_cast<uint8_t>(louder ? level + 1 : level - 1);
    deadline_.reset();
  } else if (failed && !target_ && !drained && source_active_) [[unlikely]] {
    send(Volume::Level{level});
  }
}
} // namespace avclan
