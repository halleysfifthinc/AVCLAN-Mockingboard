// copyright (C) 2006 Marcin Slonicki <marcin@softservice.com.pl>
// copyright (C) 2007 Louis Frigon
// Copyright (C) 2015 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>

#include "avclan.h"
#include "cdchanger.hpp"
#include "device.hpp"
#include "frame.hpp"
#include "hal/media.h"

namespace {
using namespace avclan;
using namespace std::chrono_literals;

constexpr uint8_t cdloading_resp[] = {to_underlying(Device::CD_CHANGER),
                                      to_underlying(Device::STATUS),
                                      to_underlying(Action::Loading_Status),
                                      0x00,
                                      0x01,
                                      0x00,
                                      0x01,
                                      0x00,
                                      0x01,
                                      0x02};

constexpr int WIRE_SIZE = 8; // cd state report size in bytes
constexpr auto TIME_SKIP = 15s;
constexpr int TWODIGIT_MAX = 99;
constexpr auto MAX_TIME = std::chrono::minutes{TWODIGIT_MAX} + 59s;

// Pack a 0–TWODIGIT_MAX count into 2-digit BCD
constexpr uint8_t toBCD(uint8_t val) {
  return (uint8_t)(((val / 10) << 4) | (val % 10));
}

// Whole seconds, saturated to the displayable range
constexpr std::chrono::seconds displaySeconds(std::chrono::milliseconds t) {
  return std::clamp(std::chrono::floor<std::chrono::seconds>(t), -MAX_TIME,
                    MAX_TIME);
}
} // namespace

namespace avclan {

void CDChanger::init(Notifier notif) {
  media_init();
  notifier = notif;
  statusTimer = xTimerCreate(
      "cd status", pdMS_TO_TICKS(1000), true, this, [](TimerHandle_t timer) {
        auto &self = *static_cast<CDChanger *>(pvTimerGetTimerID(timer));
        // Timer callbacks must not block; a refused request is re-made next
        // tick
        if (!self.statusQueued.exchange(true) && !self.notifier.notify(0, 0))
          self.statusQueued = false;
      });
  configASSERT(statusTimer);
}

void CDChanger::handle(const Frame &in, Frame &out) {
  if (in.length < 4)
    return; // [Currently known] valid CDChanger frames have at least 4 bytes

  std::optional<MediaAction> media;

  const uint8_t *data = &in.data[1];
  const auto from = static_cast<Device>(*data++);
  /* const auto to = */ data++;
  const auto action = static_cast<Action>(*data++);

  static const uint8_t function_change_resp[] = {
      0x00, to_underlying(Device::CD_CHANGER), 0xFF, 0xFF, 0x01};

  using enum Action;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"
  // Unicast to CD changer: bytes are (0x00, from, to, action, [extra...]).
  switch (const std::lock_guard lock(mutex); action) {
    case Enable_Function_Req:
      out.is_unicast = true;
      out.length = sizeof(function_change_resp);
      memcpy(out.data, function_change_resp, sizeof(function_change_resp));
      out.data[2] = to_underlying(from);
      out.data[3] = to_underlying(Enable_Function_Resp);
      state = 0;
      flags2 = 0x80;
      out.reaction = r_StatusReport;
      break;
    case Disable_Function_Req:
      // Head unit always expects a response, but the state change can be
      // conditional
      out.is_unicast = true;
      out.length = sizeof(function_change_resp);
      memcpy(out.data, function_change_resp, sizeof(function_change_resp));
      out.data[2] = to_underlying(from);
      out.data[3] = to_underlying(Disable_Function_Resp);
      if (isPlaying()) {
        media = stopPlaying();
        state = 0;
        flags2 = 0x80;
        out.reaction = r_StatusReport;
      } else
        out.reaction = r_SendOnly;
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
        out.is_unicast = true;
        {
          const uint8_t msg[] = {0x00, to_underlying(Device::CD_CHANGER),
                                 to_underlying(Device::CMD_SW),
                                 to_underlying(Insertion), 0x01};
          out.length = sizeof(msg);
          memcpy(out.data, msg, sizeof(msg));
        }
        out.reaction = r_SendOnly;
      }
      break;
    }
    case Initial_Report_Req: {
      out.is_unicast = true;
      // No knowledge/understanding of field meaning/interpretation
      const uint8_t cdinitreport_resp[] = {0x00,
                                           to_underlying(Device::CD_CHANGER),
                                           to_underlying(from),
                                           to_underlying(Initial_Report_Resp),
                                           0x01,
                                           0x31,
                                           0x10,
                                           0x01,
                                           0x01};
      out.length = sizeof(cdinitreport_resp);
      memcpy(out.data, cdinitreport_resp, sizeof(cdinitreport_resp));
      out.reaction = r_SendOnly;
      break;
    }
    case Playback_Req:
      out.data[0] = 0x00;
      out.data[1] = to_underlying(Device::CD_CHANGER);
      out.data[2] = to_underlying(from);
      out.data[3] = to_underlying(Playback_Resp);
      out.length = WIRE_SIZE + 4;
      serialize(&out.data[4]);
      out.is_unicast = true;
      out.reaction = r_SendOnly;
      break;
    case Loading_Req:
      out.data[0] = 0x00;
      out.length = sizeof(cdloading_resp) + 1;
      memcpy(&out.data[1], cdloading_resp, sizeof(cdloading_resp));
      out.data[2] = to_underlying(from);
      out.data[3] = to_underlying(Loading_Resp);
      out.is_unicast = true;
      out.reaction = r_SendOnly;
      break;
    case Track_Seek_Up:
      state = SEEKING_TRACK;
      if (track < 98)
        ++track;
      else
        track = 1;
      time.reset();
      generateStatus(out, true, Device::CMD_SW);
      media = MediaAction::Track_Next;
      out.reaction = r_TrackChange;
      break;
    case Track_Seek_Down:
      state = SEEKING_TRACK;
      // Track down returns to track beginning if in ~middle of song
      if (const auto t = trackTime(); t && *t < 5s) {
        if (track > 1)
          --track;
        else
          track = TWODIGIT_MAX;

        media = MediaAction::Track_Prev;
      }
      time.reset();
      generateStatus(out, true, Device::CMD_SW);
      out.reaction = r_TrackChange;
      break;
    case Track_Fast_Forward: {
      state |= SEEKING;
      seek(TIME_SKIP);
      generateStatus(out, true, Device::CMD_SW);
      media = MediaAction::Skip_Forward;
      out.reaction = r_SendOnly;
      break;
    }
    case Track_Rewind: {
      state |= SEEKING;
      seek(-TIME_SKIP);
      generateStatus(out, true, Device::CMD_SW);
      media = MediaAction::Skip_Backward;
      out.reaction = r_SendOnly;
      break;
    }
    case CD_Enable_Random:
      flags |= RANDOM;
      generateStatus(out, true, Device::CMD_SW);
      out.reaction = r_StatusReport;
      break;
    case CD_Disable_Random:
      flags &= ~RANDOM;
      generateStatus(out, true, Device::CMD_SW);
      out.reaction = r_StatusReport;
      break;
    case CD_Enable_Repeat:
      flags |= REPEAT;
      generateStatus(out, true, Device::CMD_SW);
      out.reaction = r_StatusReport;
      break;
    case CD_Disable_Repeat:
      flags &= ~REPEAT;
      generateStatus(out, true, Device::CMD_SW);
      out.reaction = r_StatusReport;
      break;
    case CD_Enable_Disk_Random:
      flags |= DISK_RANDOM;
      generateStatus(out, true, Device::CMD_SW);
      out.reaction = r_StatusReport;
      break;
    case CD_Disable_Disk_Random:
      flags &= ~DISK_RANDOM;
      generateStatus(out, true, Device::CMD_SW);
      out.reaction = r_StatusReport;
      break;
    case CD_Enable_Disk_Repeat:
      flags |= DISK_REPEAT;
      generateStatus(out, true, Device::CMD_SW);
      out.reaction = r_StatusReport;
      break;
    case CD_Disable_Disk_Repeat:
      flags &= ~DISK_REPEAT;
      generateStatus(out, true, Device::CMD_SW);
      out.reaction = r_StatusReport;
      break;
    case CD_Enable_Scan:
      flags |= SCAN;
      generateStatus(out, true, Device::CMD_SW);
      out.reaction = r_StatusReport;
      break;
    case CD_Disable_Scan:
      flags &= ~SCAN;
      generateStatus(out, true, Device::CMD_SW);
      out.reaction = r_StatusReport;
      break;
    case CD_Enable_Disk_Scan:
      flags |= DISK_SCAN;
      generateStatus(out, true, Device::CMD_SW);
      out.reaction = r_StatusReport;
      break;
    case CD_Disable_Disk_Scan:
      flags &= ~DISK_SCAN;
      generateStatus(out, true, Device::CMD_SW);
      out.reaction = r_StatusReport;
      break;
    default: break;
  }
#pragma GCC diagnostic pop

  // Media action handled outside of switch to minimize lock duration
  if (media)
    media_action(*media);
}

std::unique_ptr<Frame>
CDChanger::react(expected<std::unique_ptr<Frame>, detail::SendError> exp) {
  std::optional<MediaAction> media;
  std::unique_ptr<Frame> next;

  if (const std::lock_guard lock(mutex); !exp) {
    if (exp.error().reaction == to_underlying(r_StateReport) &&
        exp.error().err == detail::Error::Send::NAK_ADDRESS &&
        ++failedStatusReports > 1) {
      failedStatusReports = 0;
      media = stopPlaying(); // Disable periodic updates if e.g. no-one's
                             // listening (car was turned off?)
    }
  } else {
    auto out = std::move(exp.value());
    auto resp = static_cast<reaction_t>(out->reaction);
    out->reaction = r_Nothing;
    switch (resp) {
      case r_Ejection: {
        const uint8_t play[] = {0x00,
                                to_underlying(Device::COMM_CTRL),
                                to_underlying(Device::COMMUNICATION_V1),
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
        out->data[2] = to_underlying(Action::Loading_Status);
        out->reaction = r_SendOnly;
        break;
      case r_TrackChange:
        setTime(0ms);
        // Track starts at a whole sec; next status is ~1 sec out
        xTimerReset(statusTimer, 0);
        [[fallthrough]];
      case r_NormalizeState:
        normalizeState();
        generateStatus(*out, false, Device::STATUS);
        out->reaction = r_SendOnly;
        break;
      case r_StartPlaying:
        normalizeState();
        generateStatus(*out, false, Device::STATUS);
        out->reaction = r_BeganPlaying;
        break;
      case r_BeganPlaying:
        media = startPlaying(); // only start status timer after normalizing
        out->reaction = r_Nothing;
        break;
      case r_StatusReport:
        generateStatus(*out, false, Device::STATUS);
        out->reaction = r_SendOnly;
        break;
      case r_StateReport: [[fallthrough]];
      case r_SendOnly: [[fallthrough]];
      case r_Nothing: [[fallthrough]];
      default: break;
    }

    if (out->reaction > r_Nothing)
      next = std::move(out);
  }

  if (media)
    media_action(*media);
  return next;
}

void CDChanger::enable(Frame &out) {
  if (!isPlaying()) {
    const std::lock_guard lock(mutex);
    if (!time)
      setTime(0ms);
    state = SEEKING | SEEKING_TRACK;
    flags2 = 0x80;
    generateStatus(out, false, Device::STATUS);
    out.reaction = r_StartPlaying;
  }
}

void CDChanger::emit(Frame &out, uint32_t /*payload*/) {
  statusQueued = false; // Clear first so a tick during emit() queues anew
  const std::lock_guard lock(mutex);
  generateStatus(out, false, Device::STATUS);
  out.reaction = r_StateReport;
}

bool CDChanger::isPlaying() const { return playing; }

// Starts track time and the ~1 Hz status report (the first ~1 sec from now)
std::optional<MediaAction> CDChanger::startPlaying() {
  static bool havePlayed = false;
  std::optional<MediaAction> media;
  if (havePlayed)
    media = MediaAction::Play;
  havePlayed |= true;
  if (!playing)
    refTick = xTaskGetTickCount();
  playing = true;
  xTimerReset(statusTimer, 0);
  return media;
}

MediaAction CDChanger::stopPlaying() {
  xTimerStop(statusTimer, 0);
  time = trackTime();
  playing = false;
  return MediaAction::Pause;
}

std::optional<std::chrono::milliseconds> CDChanger::trackTime() const {
  if (!time || !playing)
    return time;
  return *time + std::chrono::milliseconds{
                     pdTICKS_TO_MS(xTaskGetTickCount() - refTick)};
}

void CDChanger::setTime(std::chrono::milliseconds t) {
  time = t;
  refTick = xTaskGetTickCount();
}

// Seek `by` from the current whole second; no-op while no time is shown
void CDChanger::seek(std::chrono::seconds by) {
  const auto t = trackTime();
  if (!t)
    return;
  setTime(displaySeconds(*t + by));
  if (playing) // Seeked to a whole sec; next status is ~1 sec out
    xTimerReset(statusTimer, 0);
}

// Serialize cd_status into the wire format.
// track/mins/secs, need converted from decimal to BCD
void CDChanger::serialize(uint8_t *dst) const {
  uint8_t mins = 0xFF; // "no time" sentinels
  uint8_t secs = 0x7F;
  bool negative = false;
  if (const auto t = trackTime()) {
    const auto sec = displaySeconds(*t);
    const auto magnitude = std::chrono::abs(sec);
    const auto wholeMins = std::chrono::floor<std::chrono::minutes>(magnitude);
    mins = toBCD((uint8_t)wholeMins.count());
    secs = toBCD((uint8_t)(magnitude - wholeMins).count());
    negative = sec < 0s;
  }

  *dst++ = cds;
  *dst++ = state;
  *dst++ = disc;
  *dst++ = toBCD(track);
  *dst++ = mins;
  *dst++ = secs;
  *dst++ = flags;
  *dst++ = negative ? (flags2 | NEGATIVE) : (flags2 & ~NEGATIVE);
}

// Used for changed status messages
void CDChanger::generateStatus(Frame &status, bool is_unicast,
                               Device to) const {
  status.is_unicast = is_unicast;
  if (!is_unicast)
    status.peripheral_addr = 0x1FF;
  status.control = 0xF;
  status.length = WIRE_SIZE + ((is_unicast) ? 4 : 3);

  uint8_t *data = status.data;
  if (is_unicast)
    *data++ = 0x00;
  *data++ = to_underlying(Device::CD_CHANGER);
  *data++ = to_underlying(to);
  *data++ = to_underlying(Action::Playback_Status);
  serialize(data);
}

void CDChanger::normalizeState() {
  if (!time)
    setTime(0ms);
  state = PLAYBACK;
  flags &= (uint8_t)~(DISK_SCAN | SCAN);
}

#ifndef NDEBUG
void CDChanger::media_action(MediaAction action) { ::media_action(action); }
bool CDChanger::media_busy() const { return ::media_busy(); };
void CDChanger::mic_toggle() { media_mic_toggle(); };
#endif

} // namespace avclan
