// copyright (C) 2006 Marcin Slonicki <marcin@softservice.com.pl>
// copyright (C) 2007 Louis Frigon
// Copyright (C) 2015 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>

#include "FreeRTOS.h" // IWYU pragma: export
#include "timers.h"

#include "avclan.h"
#include "device.hpp"
#include "frame.hpp"
#include "media/binding.hpp"
#include "media/playback.hpp"

namespace avclan {

class CDChanger : public Notifier<CDChanger>,
                  public media::View<CDChanger, media::Playback> {
  using Playback = media::Playback;

public:
  enum State : uint8_t {
    OPEN = 0x01,
    ERR1 = 0x02,
    SEEKING = 0x08,
    PLAYBACK = 0x10,
    SEEKING_TRACK = 0x20,
    LOADING = 0x80,
  };
  enum CD : uint8_t {
    CD1 = 1 << 0,
    CD2 = 1 << 1,
    CD3 = 1 << 2,
    CD4 = 1 << 3,
    CD5 = 1 << 4,
    CD6 = 1 << 5,
  };
  enum Flags : uint8_t {
    DISK_RANDOM = 1 << 1,
    RANDOM = 1 << 2,
    DISK_REPEAT = 1 << 3,
    REPEAT = 1 << 4,
    DISK_SCAN = 1 << 5,
    SCAN = 1 << 6,
    NEGATIVE = 1 << 6, // in flags2
  };

  /// Message state machine
  // - r_Nothing (0x00) means don't send current message
  // - All other instances mean send current message and imply the presence of
  //   follow-up messages within state machine
  enum reaction_t : uint8_t {
    r_Nothing = 0x00,
    r_SendOnly,       // No further follow-up needed (beyond sending current)
    r_StatusReport,   // Needs follow-up status report
    r_NormalizeState, // cd_status needs normalized and resent
    r_StartPlaying, // ~equivalent to normalizeState, but cycles to BeganPlaying
    r_BeganPlaying,
    r_TrackChange, // Time needs reset
    r_Ejection,
    r_Report_Load,
    r_StateReport, // *IS* a status report (follow-up or unprompted)
    r_Drain,       // Condition only, never a frame's: apply pending reports
  };

  static constexpr Device id = Device::CD_CHANGER;
  static constexpr uint8_t conditions = 3; // r_StateReport, r_Ejection, r_Drain
  static constexpr uint8_t reserve_events = 0;
  CDChanger() = default;
  CDChanger(const CDChanger &) = delete;

  void init(NotifyQueues queues);

  void handle(const Frame &in, Frame &out);
  std::unique_ptr<Frame>
  react(expected<std::unique_ptr<Frame>, detail::SendError> exp);
  void reported_active(Frame &out);

  void receive(const Playback::Shuffle &s);
  void receive(const Playback::Repeat &r);
  void receive(const Playback::Track &t);
  void receive(const Playback::Position &p);
  void receive(const Playback::Playing &p);
  void receive(const Playback::Connected &c);

private:
  friend Notifier<CDChanger>;
  friend media::View<CDChanger, Playback>;

  void wake(const Playback::Report &msg);
  void fill(Frame &out, condition_t tag, uint32_t payload);

  void startPlaying();
  void stopPlaying();
  std::optional<std::chrono::milliseconds> trackTime() const;
  void setTime(std::chrono::milliseconds t);
  void rebaseTime();
  void seek(std::chrono::milliseconds to);
  void beginSeeking(Playback::Direction direction);
  void endSeeking();
  void serialize(uint8_t *dst) const;
  void generateStatus(Frame &status, bool is_unicast, Device to) const;
  void normalizeState();

  TimerHandle_t statusTimer = nullptr;

  // Track time is a stopwatch: `time` is relative to `refTick`, and advances
  // while `playing` and the source isn't paused. nullopt means no time is
  // shown. rebaseTime() must precede any change to `playing` or
  // `sourcePlaying`.
  std::optional<std::chrono::milliseconds> time;
  TickType_t refTick = 0;
  // The head unit activated us (activation began)
  bool active = false;
  // Activation completed; status timer runs
  bool playing = false;
  // What the source last reported; nullopt: no reports (the clock free-runs)
  std::optional<bool> sourcePlaying;
  int failedStatusReports = 0;
  uint8_t cds = CD1;
  uint8_t state = SEEKING | SEEKING_TRACK;
  // FF/RW held down. SEEKING (state) is also set outside of FF/RW.
  Playback::Direction seeking = Playback::Direction::None;
  uint8_t disc = 1;
  uint8_t track = 1; // Decimal storage; serialize to BCD. 0: no track (source
                     // has no active player)
  uint8_t flags = 0;
  // The random bits the head unit last asked for; shown when shuffle is on
  uint8_t requestedRandom = RANDOM;
  uint8_t flags2 = 0x80;
};

} // namespace avclan
