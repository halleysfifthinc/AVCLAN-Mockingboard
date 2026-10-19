// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include "FreeRTOS.h" // IWYU pragma: export
#include "timers.h"

#include "avclan.h"
#include "device.hpp"
#include "media/binding.hpp"
#include "media/volume.hpp"

namespace avclan {
class Amplifier : public Notifier<Amplifier>,
                  public media::Model<Amplifier, media::Volume> {
  using Volume = media::Volume;

public:
  static constexpr Device id = Device::AUDIO_AMP;
  static constexpr uint8_t conditions = 2;
  static constexpr uint8_t reserve_events = 0;
  static constexpr bool listed = false;

  Amplifier() = default;
  Amplifier(const Amplifier &) = delete;

  void init(NotifyQueues queues);

  // `source` is the function whose audio the binding's volume controls
  template <DeviceInterface Source>
  void attach(media::Binding<Volume> &b, const Source & /* source */) {
    source_ = Source::id;
    media::Model<Amplifier, Volume>::attach(b);
  }

  std::unique_ptr<Frame>
  react(expected<std::unique_ptr<Frame>, detail::SendError> exp);

  void observe(const Frame &);
  void active_changed(Device function);

  void receive(const Volume::Set &s);
  void receive(const Volume::Reset &);

private:
  friend Notifier<Amplifier>;
  friend media::Model<Amplifier, Volume>;

  enum conditions_t : uint8_t {
    r_VolumeRequest,
    r_FailedVolume,
  };

  void wake(const Volume::Request &msg);
  void fill(Frame &out, condition_t tag, uint32_t payload);

  void clear_pending();
  void sync();

  std::optional<uint16_t> amp_addr_;

  // Written once
  Device source_ = NoDevice;
  // Phone volume applies only while `source_` is the active function
  bool source_active_ = false;

  // Possibly indicated in first byte (post from/to/action header) seen as 0xb0
  // => 0x90 (bit 5 cleared) in transition from no active device to cdchanger
  // active
  bool amplifier_muted_ = true;

  // The amp's last reported volume byte (see `attenuation`)
  std::optional<uint8_t> volume_;
  // The phone's level. Held while `source_` isn't active; cleared once
  // reached, or when the head unit changes the volume while active.
  std::optional<uint8_t> target_;
  // The level the pending step requested; set while it's pending
  std::optional<uint8_t> requested_;
  // The latest set, carried by its frame as `reaction`
  uint8_t set_id_ = 0;
  // Set by react() once sent. A timeout/failure before then, or before this
  // tick, is an earlier set's.
  std::optional<TickType_t> deadline_;

  TimerHandle_t timeout_ = nullptr;

  // Balance/fade/EQ are encoded offset16 on the wire, observed ranges are
  // +-5 (EQ) or +-7 (balance/fade)
  std::optional<int8_t> balance_ = std::nullopt;
  std::optional<int8_t> fade_ = std::nullopt;
  std::optional<int8_t> bass_ = std::nullopt;
  std::optional<int8_t> mid_ = std::nullopt;
  std::optional<int8_t> treble_ = std::nullopt;
};
} // namespace avclan
