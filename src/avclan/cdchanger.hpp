// copyright (C) 2006 Marcin Slonicki <marcin@softservice.com.pl>
// copyright (C) 2007 Louis Frigon
// Copyright (C) 2015 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

#include "avclan.hpp"
#include "avclan_defs.h"
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

  static constexpr uint8_t id = dev_CD_CHANGER;
  void init();

  void handle(const Frame *in, Frame *out);
  void react(Frame *out, detail::Error::Send err);
  void enable(Frame *out);
  void disable(Frame *out);
  static bool pending();
  static void resolvepending();
  void emit(Frame *out, uint16_t peripheral);
  void incrementTime();
  bool isPlaying() const;

private:
  void startPlaying();
  void stopPlaying();
  void serialize(uint8_t *dst) const;
  void setTime(uint8_t mins, uint8_t secs);
  void generateStatus(Frame *status, bool is_unicast, devices to) const;
  void normalizeState();

  bool playing = false;
  int failedStatusReports = 0;
  uint8_t cds = CD1;
  uint8_t state = SEEKING | SEEKING_TRACK;
  uint8_t disc = 1;
  uint8_t track = 1;   // Decimal storage; serialize to BCD
  uint8_t mins = 0xFF; // Decimal storage; serialize to BCD
  uint8_t secs = 0x7F; // Decimal storage; serialize to BCD
  uint8_t flags = 0;
  uint8_t flags2 = 0xC0;
};

} // namespace avclan
