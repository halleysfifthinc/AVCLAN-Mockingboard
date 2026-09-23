// copyright (C) 2006 Marcin Slonicki <marcin@softservice.com.pl>
// copyright (C) 2007 Louis Frigon
// Copyright (C) 2015 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>

#include "FreeRTOS.h" // IWYU pragma: export
#include "semphr.h"
#include "timers.h"

#include "avclan.h"
#include "device.hpp"
#include "frame.hpp"

namespace avclan {

class CDChanger {
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
    NEGATIVE = 1 << 6,
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
  };

  static constexpr Device id = Device::CD_CHANGER;
  CDChanger() : mutex(xSemaphoreCreateMutex()) {}
  CDChanger(const CDChanger &) = delete;

  void init(Notifier notifier);

  void handle(const Frame &in, Frame &out);
  std::unique_ptr<Frame>
  react(expected<std::unique_ptr<Frame>, detail::SendError> exp);
  void enable(Frame &out);
  void disable(Frame &out);
  void emit(Frame &out, uint32_t payload);
  bool isPlaying() const;
#ifndef NDEBUG
  void media_action(MediaAction action);
  bool media_busy() const;
  void mic_toggle();
#endif

private:
  // Implements the BasicLockable named requirements for an xSemaphore
  class xMutex {
  public:
    explicit xMutex(SemaphoreHandle_t mutex) : mutex_{mutex} {}
    xMutex(const xMutex &) = delete;

    void lock() noexcept { xSemaphoreTake(mutex_, portMAX_DELAY); }
    void unlock() noexcept { xSemaphoreGive(mutex_); }

  private:
    SemaphoreHandle_t mutex_ = nullptr;
  };

  std::optional<MediaAction> startPlaying();
  MediaAction stopPlaying();
  std::optional<std::chrono::milliseconds> trackTime() const;
  void setTime(std::chrono::milliseconds t);
  void seek(std::chrono::seconds by);
  void serialize(uint8_t *dst) const;
  void generateStatus(Frame &status, bool is_unicast, Device to) const;
  void normalizeState();

  Notifier notifier{};
  TimerHandle_t statusTimer = nullptr;
  std::atomic<bool> statusQueued = false; // coalesces status emit requests
  xMutex mutex;

  // Track time is a stopwatch: `time` is relative to `refTick`, and
  // advances while `playing`. nullopt means no time is shown.
  std::optional<std::chrono::milliseconds> time;
  TickType_t refTick = 0;
  bool playing = false;
  int failedStatusReports = 0;
  uint8_t cds = CD1;
  uint8_t state = SEEKING | SEEKING_TRACK;
  uint8_t disc = 1;
  uint8_t track = 1; // Decimal storage; serialize to BCD
  uint8_t flags = 0;
  uint8_t flags2 = 0x80;
};

} // namespace avclan
