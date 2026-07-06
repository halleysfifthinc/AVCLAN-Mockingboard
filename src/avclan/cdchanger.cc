// copyright (C) 2006 Marcin Slonicki <marcin@softservice.com.pl>
// copyright (C) 2007 Louis Frigon
// Copyright (C) 2015 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdint>
#include <cstring>

#include "avclan.h"
#include "cdchanger.hpp"
#include "device.hpp"
#include "frame.hpp"
#include "mediacontrol.h"
#include "statustimer.h"

namespace {
using namespace avclan;

constexpr uint8_t cdloading_resp[] = {
    to_underlying(Device::CD_CHANGER),
    to_underlying(Device::STATUS),
    to_underlying(Action::Loading_Status_Report),
    0x00,
    0x01,
    0x00,
    0x01,
    0x00,
    0x01,
    0x02};

constexpr int WIRE_SIZE = 8;  // cd state report size in bytes
constexpr int TIME_SKIP = 15; // seconds
constexpr int TWODIGIT_MAX = 99;

/* Pack a 0–TWODIGIT_MAX count into 2-digit BCD. Values >TWODIGIT_MAX (sentinels
   such as 0xFF / 0x7F meaning "no time") pass through unchanged so they survive
   the wire round-trip. */
constexpr uint8_t toBCD(uint8_t val) {
  if (val > TWODIGIT_MAX)
    return val;
  return (uint8_t)(((val / 10) << 4) | (val % 10));
}

extern "C" void incrementTime_callback(void *self) {
  static_cast<avclan::CDChanger *>(self)->incrementTime();
}

extern "C" bool isPlaying_callback(void *self) {
  return static_cast<avclan::CDChanger *>(self)->isPlaying();
}
} // namespace

namespace avclan {

void CDChanger::init() {
  mediacontrol_init();
  statustimer_init(this, &incrementTime_callback, &isPlaying_callback);
}

void CDChanger::handle(const Frame *in, Frame *out) {
  const uint8_t *data = &in->data[1];
  const auto from = static_cast<Device>(*data++);
  /* const auto to = */ data++;
  const auto action = static_cast<Action>(*data++);

  static const uint8_t function_change_resp[] = {
      0x00, to_underlying(Device::CD_CHANGER), to_underlying(from), 0xFF, 0x01};

  using enum Action;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"
  // Unicast to CD changer: bytes are (0x00, from, to, action, [extra...]).
  switch (action) {
    case Enable_Function_Req:
      out->is_unicast = true;
      out->length = sizeof(function_change_resp);
      memcpy(out->data, function_change_resp, sizeof(function_change_resp));
      out->data[3] = to_underlying(Enable_Function_Resp);
      state = 0;
      flags2 = 0x80;
      out->reaction = r_StatusReport;
      break;
    case Disable_Function_Req:
      // No change/response needed if we're already not playing
      if (isPlaying()) {
        stopPlaying();
        out->length = sizeof(function_change_resp);
        memcpy(out->data, function_change_resp, sizeof(function_change_resp));
        out->data[3] = to_underlying(Disable_Function_Resp);
        state = 0;
        flags2 = 0x80;
        out->is_unicast = true;
        out->reaction = r_StatusReport;
      }
      break;
    case Eject: {
      // "Eject" label is multiply wrong; proper meaning unclear:
      //    - First observed on initial multiple presses of "CD" button,
      //    triggering (after {0x00, Device::CD_CHANGER, Device::COMM_v1,
      //    Insertion, 0x01} response) proper activation of
      //    mockingboard/cd-changer.
      //    - Subsequently observed when pressing (technically
      //    releasing?) the fast-forward button and rewind
      if (static_cast<bool>(state & SEEKING)) { // FF/RW button released
        state &= ~SEEKING;
      } else {
        out->is_unicast = true;
        {
          const uint8_t msg[] = {0x00, to_underlying(Device::CD_CHANGER),
                                 to_underlying(Device::CMD_SW),
                                 to_underlying(Insertion), 0x01};
          out->length = sizeof(msg);
          memcpy(out->data, msg, sizeof(msg));
        }
        out->reaction = r_SendOnly;
      }
      break;
    }
    case Initial_Report_Request: {
      out->is_unicast = true;
      // No knowledge/understanding of field meaning/interpretation
      const uint8_t cdinitreport_resp[] = {
          0x00,
          to_underlying(Device::CD_CHANGER),
          to_underlying(from),
          to_underlying(Initial_Report_Response),
          0x01,
          0x31,
          0x10,
          0x01,
          0x01};
      out->length = sizeof(cdinitreport_resp);
      memcpy(out->data, cdinitreport_resp, sizeof(cdinitreport_resp));
      out->reaction = r_SendOnly;
      break;
    }
    case Playback_Request:
      out->data[0] = 0x00;
      out->data[1] = to_underlying(Device::CD_CHANGER);
      out->data[2] = to_underlying(from);
      out->data[3] = to_underlying(Playback_Report);
      out->length = WIRE_SIZE + 4;
      serialize(&out->data[4]);
      out->is_unicast = true;
      out->reaction = r_SendOnly;
      break;
    case Loading_Request2:
      out->data[0] = 0x00;
      out->length = sizeof(cdloading_resp) + 1;
      memcpy(&out->data[1], cdloading_resp, sizeof(cdloading_resp));
      out->data[2] = to_underlying(from);
      out->data[3] = to_underlying(Loading_Response2);
      out->is_unicast = true;
      out->reaction = r_SendOnly;
      break;
    case Track_Seek_Up:
      state = SEEKING_TRACK;
      if (track < 98)
        ++track;
      else
        track = 1;
      mins = 0xff;
      secs = 0x7f;
      flags2 = 0xc0;
      generateStatus(out, true, Device::CMD_SW);
      AVCLAN_mediaFunction(MEDIA_SKIP_FORWARD);
      out->reaction = r_TrackChange;
      break;
    case Track_Seek_Down:
      state = SEEKING_TRACK;
      // Track down returns to track beginning if in ~middle of song
      if (mins == 0 && secs < 5) {
        if (track > 1)
          --track;
        else
          track = TWODIGIT_MAX;
      }
      mins = 0xff;
      secs = 0x7f;
      flags2 = 0xc0;
      generateStatus(out, true, Device::CMD_SW);
      AVCLAN_mediaFunction(MEDIA_SKIP_BACKWARD);
      out->reaction = r_TrackChange;
      break;
    case Track_Fast_Forward: {
      state |= SEEKING;
      secs += TIME_SKIP;
      if (secs > 60) {
        secs -= 60;
        ++mins;
      }
      generateStatus(out, true, Device::CMD_SW);
      AVCLAN_mediaFunction(MEDIA_SKIP_FORWARD);
      statustimer_reset(); // Skipped to a whole/round sec; ensure next tick
                           // is ~1 sec from now
      out->reaction = r_SendOnly;
      break;
    }
    case Track_Rewind: {
      state |= SEEKING;
      if (secs < TIME_SKIP) {
        if (mins > 0) {
          const uint8_t dif = TIME_SKIP - secs;
          secs = 60 - dif;
          --mins;
        } else {
          mins = 0;
          secs = 0;
        }
      } else
        secs -= TIME_SKIP;
      generateStatus(out, true, Device::CMD_SW);
      AVCLAN_mediaFunction(MEDIA_SKIP_BACKWARD);
      statustimer_reset(); // Skipped to a whole/round sec; ensure next tick
                           // is ~1 sec from now
      out->reaction = r_SendOnly;
      break;
    }
    case CD_Enable_Random:
      flags |= RANDOM;
      generateStatus(out, true, Device::CMD_SW);
      out->reaction = r_StatusReport;
      break;
    case CD_Disable_Random:
      flags &= ~RANDOM;
      generateStatus(out, true, Device::CMD_SW);
      out->reaction = r_StatusReport;
      break;
    case CD_Enable_Repeat:
      flags |= REPEAT;
      generateStatus(out, true, Device::CMD_SW);
      out->reaction = r_StatusReport;
      break;
    case CD_Disable_Repeat:
      flags &= ~REPEAT;
      generateStatus(out, true, Device::CMD_SW);
      out->reaction = r_StatusReport;
      break;
    case CD_Enable_Disk_Random:
      flags |= DISK_RANDOM;
      generateStatus(out, true, Device::CMD_SW);
      out->reaction = r_StatusReport;
      break;
    case CD_Disable_Disk_Random:
      flags &= ~DISK_RANDOM;
      generateStatus(out, true, Device::CMD_SW);
      out->reaction = r_StatusReport;
      break;
    case CD_Enable_Disk_Repeat:
      flags |= DISK_REPEAT;
      generateStatus(out, true, Device::CMD_SW);
      out->reaction = r_StatusReport;
      break;
    case CD_Disable_Disk_Repeat:
      flags &= ~DISK_REPEAT;
      generateStatus(out, true, Device::CMD_SW);
      out->reaction = r_StatusReport;
      break;
    default: break;
  }
#pragma GCC diagnostic pop
}

void CDChanger::react(Frame *out, detail::Error::Send err) {
  auto resp = static_cast<reaction_t>(out->reaction);
  out->reaction = r_Nothing;
  switch (resp) {
    case r_StateReport:
      if (err == detail::Error::Send::NAK_ADDRESS &&
          ++failedStatusReports > 1) {
        failedStatusReports = 0;
        stopPlaying(); // Disable periodic updates if e.g. no-one's
                       // listening (car was turned off?)
      }
      break;
    case r_Ejection: {
      const uint8_t play[] = {0x00,
                              to_underlying(Device::COMM_CTRL),
                              to_underlying(Device::COMM_v1),
                              to_underlying(Action::Insertion),
                              to_underlying(Device::CD_CHANGER),
                              0x01};
      out->length = sizeof(play);
      memcpy(out->data, play, sizeof(play));
    }
      out->reaction = r_Report_Load;
      break;
    case r_Report_Load:
      out->is_unicast = false;
      out->peripheral_addr = 0x1FF;
      out->length = sizeof(cdloading_resp) + 1;
      memcpy(out->data, cdloading_resp, sizeof(cdloading_resp));
      out->data[1] = to_underlying(Device::STATUS);
      out->data[2] = to_underlying(Action::Loading_Status_Report);
      out->reaction = r_SendOnly;
      break;
    case r_TrackChange:
      setTime(0, 0);
      statustimer_reset(); // Skipped to a whole/round sec; ensure next tick is
                           // ~1 sec from now
      [[fallthrough]];
    case r_NormalizeState:
      normalizeState();
      generateStatus(out, true, Device::STATUS);
      out->reaction = r_SendOnly;
      break;
    case r_StartPlaying:
      normalizeState();
      generateStatus(out, true, Device::STATUS);
      out->reaction = r_BeganPlaying;
      break;
    case r_BeganPlaying:
      startPlaying(); // only start PIT after normalizing state
      out->reaction = r_Nothing;
      break;
    case r_StatusReport:
      generateStatus(out, true, Device::STATUS);
      out->reaction = r_SendOnly;
      break;
    case r_SendOnly: [[fallthrough]];
    case r_Nothing: [[fallthrough]];
    default: out->reaction = r_Nothing;
  }
}

void CDChanger::enable(Frame *out) {
  if (!isPlaying()) {
    if (mins > TWODIGIT_MAX)
      mins = 0;
    if (secs > TWODIGIT_MAX)
      secs = 0;
    state = SEEKING | SEEKING_TRACK;
    flags2 = 0xc0;
    generateStatus(out, true, Device::STATUS);
    out->reaction = r_StartPlaying;
  }
}

bool CDChanger::pending() { return statustimer_tickPending(); }
void CDChanger::resolvepending() { statustimer_clearTick(); }

void CDChanger::emit(Frame *out, uint16_t peripheral) {
  out->peripheral_addr = peripheral;
  generateStatus(out, true, Device::STATUS);
  out->reaction = r_StateReport;
}

bool CDChanger::isPlaying() const { return playing; }

// Sets CD_mode to play and resets timer count (so that the next interrupt is in
// 1 sec)
void CDChanger::startPlaying() {
  static bool havePlayed = false;
  if (havePlayed)
    AVCLAN_mediaFunction(MEDIA_PLAY_PAUSE);
  havePlayed |= true;
  playing = true;
  statustimer_reset();
}

void CDChanger::stopPlaying() {
  statustimer_disable();
  playing = false;
  AVCLAN_mediaFunction(MEDIA_PLAY_PAUSE);
}

// Serialize cd_status into the wire format.
// track/mins/secs, need converted from decimal to BCD
void CDChanger::serialize(uint8_t *dst) const {
  *dst++ = cds;
  *dst++ = state;
  *dst++ = disc;
  *dst++ = toBCD(track);
  *dst++ = toBCD(mins);
  *dst++ = toBCD(secs);
  *dst++ = flags;
  *dst++ = flags2;
}

void CDChanger::setTime(uint8_t min, uint8_t sec) {
  mins = min;
  secs = sec;
}

void CDChanger::incrementTime() {
  // Sentinel values (>TWODIGIT_MAX) mean "no time"; leave them alone until
  // setTime() replaces them with a real count.
  if (secs > TWODIGIT_MAX)
    return;
  if (secs == 59) {
    secs = 0;
    if (mins == TWODIGIT_MAX)
      mins = 0;
    else
      mins++;
  } else
    secs++;
}

// Used for changed status messages
void CDChanger::generateStatus(Frame *status, bool is_unicast,
                               Device to) const {
  status->is_unicast = is_unicast;
  if (!is_unicast)
    status->peripheral_addr = 0x1FF;
  status->control = 0xF;
  status->length = WIRE_SIZE + ((is_unicast) ? 4 : 3);

  uint8_t *data = status->data;
  if (is_unicast)
    *data++ = 0x00;
  *data++ = to_underlying(Device::CD_CHANGER);
  *data++ = to_underlying(to);
  *data++ = to_underlying(Action::Status_Report);
  serialize(data);
}

void CDChanger::normalizeState() {
  if (mins > TWODIGIT_MAX)
    mins = 0;
  if (secs > TWODIGIT_MAX)
    secs = 0;
  state = PLAYBACK;
  flags &= (uint8_t)~(DISK_SCAN | SCAN);
  flags2 = 0x80;
}

} // namespace avclan
