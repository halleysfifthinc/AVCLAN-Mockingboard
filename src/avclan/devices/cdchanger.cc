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
#include <optional>

#include "avclan.h"
#include "cdchanger.hpp"
#include "device.hpp"
#include "frame.hpp"
#include "media/playback.hpp"

namespace {
using namespace avclan;
using namespace std::chrono_literals;
using Playback = media::Playback;

constexpr uint8_t cdloading_resp[] = {
    to_underlying(Device::CD_CHANGER),
    to_underlying(Device::STATUS),
    to_underlying(Action::Loading_Status),
    0x00,
    0x01,
    0x00,
    0x01,
    0x00,
    0x01,
    0x02,
};

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

void CDChanger::init(NotifyQueues queues) {
  Notifier::init(queues);
  // A refused request is re-made by the next tick
  statusTimer = xTimerCreate(
      "cd status", pdMS_TO_TICKS(1000), true, this, [](TimerHandle_t timer) {
        static_cast<CDChanger *>(pvTimerGetTimerID(timer))
            ->notify(condition, r_StateReport);
      });
  configASSERT(statusTimer);
}

void CDChanger::handle(const Frame &in, Frame &out) {
  if (in.length < 4)
    return; // [Currently known] valid CDChanger frames have at least 4 bytes

  const uint8_t *data = &in.data[1];
  const auto from = static_cast<Device>(*data++);
  /* const auto to = */ data++;
  const auto action = static_cast<Action>(*data++);

  static const uint8_t function_change_resp[] = {
      0x00, to_underlying(Device::CD_CHANGER), 0xFF, 0xFF, 0x01};

  // The source has one repeat setting for both the disc and whole-changer
  // flags. `flags` changes only once the source reports.
  const auto requestRepeat = [this](uint8_t want) {
    if ((want & REPEAT) != 0)
      send(Playback::SetRepeat{Playback::RepeatMode::Track});
    else if ((want & DISK_REPEAT) != 0)
      send(Playback::SetRepeat{Playback::RepeatMode::All});
    else
      send(Playback::SetRepeat{Playback::RepeatMode::Off});
  };

  using enum Action;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"
  // Unicast to CD changer: bytes are (0x00, from, to, action, [extra...]).
  switch (const std::lock_guard lock(mutex); action) {
    case Activate_Function_Req:
      out.is_unicast = true;
      out.length = sizeof(function_change_resp);
      memcpy(out.data, function_change_resp, sizeof(function_change_resp));
      out.data[2] = to_underlying(from);
      out.data[3] = to_underlying(Activate_Function_Resp);
      active = true;
      endSeeking();
      state = 0;
      flags2 = 0x80;
      out.reaction = r_StatusReport;
      break;
    case Deactivate_Function_Req:
      // Head unit always expects a response, but the state change can be
      // conditional
      out.is_unicast = true;
      out.length = sizeof(function_change_resp);
      memcpy(out.data, function_change_resp, sizeof(function_change_resp));
      out.data[2] = to_underlying(from);
      out.data[3] = to_underlying(Deactivate_Function_Resp);
      active = false;
      if (playing) {
        stopPlaying();
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
      if (seeking != Playback::Direction::None) { // FF/RW button released
        endSeeking();
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
    // The track number changes only once the source reports. Previous may
    // instead restart the track.
    case Track_Seek_Up:
      endSeeking();
      state = SEEKING_TRACK;
      time.reset();
      generateStatus(out, true, Device::CMD_SW);
      send(Playback::Next{});
      out.reaction = r_TrackChange;
      break;
    case Track_Seek_Down:
      endSeeking();
      state = SEEKING_TRACK;
      time.reset();
      send(Playback::Previous{});
      generateStatus(out, true, Device::CMD_SW);
      out.reaction = r_TrackChange;
      break;
    case Track_Fast_Forward: {
      beginSeeking(Playback::Direction::Forward);
      generateStatus(out, true, Device::CMD_SW);
      out.reaction = r_SendOnly;
      break;
    }
    case Track_Rewind: {
      beginSeeking(Playback::Direction::Backward);
      generateStatus(out, true, Device::CMD_SW);
      out.reaction = r_SendOnly;
      break;
    }
    // The source has one shuffle setting, shown as RANDOM for either button
    case CD_Enable_Random:
    case CD_Enable_Disk_Random:
      generateStatus(out, true, Device::CMD_SW);
      send(Playback::SetShuffle{true});
      out.reaction = r_StatusReport;
      break;
    case CD_Disable_Random:
    case CD_Disable_Disk_Random:
      generateStatus(out, true, Device::CMD_SW);
      send(Playback::SetShuffle{false});
      out.reaction = r_StatusReport;
      break;
    case CD_Enable_Repeat:
      generateStatus(out, true, Device::CMD_SW);
      requestRepeat(flags | REPEAT);
      out.reaction = r_StatusReport;
      break;
    case CD_Disable_Repeat:
      generateStatus(out, true, Device::CMD_SW);
      requestRepeat(flags & ~REPEAT);
      out.reaction = r_StatusReport;
      break;
    case CD_Enable_Disk_Repeat:
      generateStatus(out, true, Device::CMD_SW);
      requestRepeat(flags | DISK_REPEAT);
      out.reaction = r_StatusReport;
      break;
    case CD_Disable_Disk_Repeat:
      generateStatus(out, true, Device::CMD_SW);
      requestRepeat(flags & ~DISK_REPEAT);
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
}

std::unique_ptr<Frame>
CDChanger::react(expected<std::unique_ptr<Frame>, detail::SendError> exp) {
  std::unique_ptr<Frame> next;

  if (const std::lock_guard lock(mutex); !exp) {
    if (exp.error().reaction == to_underlying(r_StateReport) &&
        exp.error().err == detail::Error::Send::NAK_ADDRESS &&
        ++failedStatusReports > 1) {
      failedStatusReports = 0;
      active = false;
      stopPlaying(); // Disable periodic updates if e.g. no-one's listening
                     // (car was turned off?)
    }
  } else {
    auto out = std::move(exp.value());
    if (!out->is_unicast && out->length == WIRE_SIZE + 3 &&
        out->data[2] == to_underlying(Action::Playback_Status))
      std::copy_n(&out->data[3], WIRE_SIZE, sentStatus.begin());
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
        if (track != 0)
          seek(0ms);
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
        startPlaying(); // only start status timer after normalizing
        out->reaction = r_Nothing;
        break;
      case r_StatusReport:
        generateStatus(*out, false, Device::STATUS);
        out->reaction = r_SendOnly;
        break;
      case r_StateReport: [[fallthrough]];
      case r_Drain: [[fallthrough]];
      case r_SendOnly: [[fallthrough]];
      case r_Nothing: [[fallthrough]];
      default: break;
    }

    if (out->reaction > r_Nothing)
      next = std::move(out);
  }

  return next;
}

void CDChanger::reported_active(Frame &out) {
  const std::lock_guard lock(mutex);
  if (playing)
    return;
  active = true;
  if (!time && track != 0)
    seek(0ms);
  state = SEEKING | SEEKING_TRACK;
  flags2 = 0x80;
  generateStatus(out, false, Device::STATUS);
  out.reaction = r_StartPlaying;
}

void CDChanger::wake(const Playback::Report & /* msg */) {
  notify(condition, r_Drain);
}

// `payload` is the reaction of the frame to emit, or r_Drain
void CDChanger::fill(Frame &out, condition_t /*tag*/, uint32_t payload) {
  const bool received = drain();
  if (payload == r_Drain) {
    // Through the condition, so it merges with a pending ~1 Hz status
    if (received && playing)
      notify(condition, r_StateReport);
    return;
  }
  if (payload == r_Ejection) {
    if (active) // Activation is already underway
      return;
    out.is_unicast = true;
    const uint8_t eject[] = {
        0x00,
        to_underlying(Device::COMM_CTRL),
        to_underlying(Device::COMMUNICATION_V1),
        to_underlying(Action::Ejection),
        to_underlying(Device::CD_CHANGER),
        0x01,
    };
    out.length = sizeof(eject);
    memcpy(out.data, eject, sizeof(eject));
    out.reaction = r_Ejection;
    return;
  }
  if (!playing) // Deactivated after the status was requested
    return;
  generateStatus(out, false, Device::STATUS);
  if (std::equal(sentStatus.begin(), sentStatus.end(), &out.data[3]))
    return;
  out.reaction = r_StateReport;
  // Restarts the ~1 Hz interval, so reports that each emit a status don't add
  // to it
  if ((state & SEEKING) == 0)
    xTimerReset(statusTimer, 0);
}

// Starts track time and the ~1 Hz status report (the first ~1 sec from now)
void CDChanger::startPlaying() {
  rebaseTime();
  playing = true;
  xTimerReset(statusTimer, 0);
  send(Playback::Play{});
}

void CDChanger::stopPlaying() {
  endSeeking();
  xTimerStop(statusTimer, 0);
  rebaseTime();
  playing = false;
  send(Playback::Pause{});
}

std::optional<std::chrono::milliseconds> CDChanger::trackTime() const {
  if (!time || !playing || !sourcePlaying.value_or(true) ||
      (state & SEEKING) != 0)
    return time;
  return *time + std::chrono::milliseconds{
                     pdTICKS_TO_MS(xTaskGetTickCount() - refTick)};
}

void CDChanger::setTime(std::chrono::milliseconds t) {
  time = t;
  refTick = xTaskGetTickCount();
}

// Folds elapsed time into `time` without moving it
void CDChanger::rebaseTime() {
  if (time)
    setTime(*trackTime());
}

// The displayed second will have changed by the next status, ~1 sec out
void CDChanger::seek(std::chrono::milliseconds to) {
  setTime(to);
  if (playing && (state & SEEKING) == 0)
    xTimerReset(statusTimer, 0);
}

// While seeking, time holds the last source report and only reports emit
// statuses. What FF/RW does depends on the source app (and its settings), so
// the time isn't guessed.
void CDChanger::beginSeeking(Playback::Direction direction) {
  rebaseTime();
  state |= SEEKING;
  seeking = direction;
  xTimerStop(statusTimer, 0);
  send(Playback::Seek{direction});
}

void CDChanger::endSeeking() {
  if (seeking != Playback::Direction::None) {
    seeking = Playback::Direction::None;
    send(Playback::Seek{Playback::Direction::None});
  }
  if ((state & SEEKING) == 0)
    return;
  rebaseTime();
  state &= ~SEEKING;
  if (playing)
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
  // TODO: try 0x7F/0xFF to blank the track digits
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
  endSeeking();
  if (!time && track != 0)
    seek(0ms);
  state = PLAYBACK;
  flags &= (uint8_t)~(DISK_SCAN | SCAN);
}

// The receive()s run under `mutex`, from drain()

void CDChanger::receive(const Playback::Shuffle &s) {
  flags &= (uint8_t)~RANDOM;
  if (s.on)
    flags |= RANDOM;
}

void CDChanger::receive(const Playback::Repeat &r) {
  flags &= (uint8_t)~(REPEAT | DISK_REPEAT);
  switch (r.mode) {
    case Playback::RepeatMode::Track: flags |= REPEAT; break;
    case Playback::RepeatMode::All: flags |= DISK_REPEAT; break;
    case Playback::RepeatMode::Off: break;
  }
}

void CDChanger::receive(const Playback::Track &trk) {
  if (trk.number == 0) { // No track selected: show no time or track
    time.reset();
    track = 0;
    return;
  }
  const auto wrapped = (uint8_t)(((trk.number - 1) % TWODIGIT_MAX) + 1);
  if (wrapped == track)
    return;
  track = wrapped;
  seek(0ms);
}

void CDChanger::receive(const Playback::Position &p) {
  const auto &pos = p.position;
  if (!pos) { // No active player: show no time or track
    time.reset();
    track = 0;
    return;
  }
  // Every report counts while seeking, but drift <1s from source position
  // reports is left alone, so periodic reports don't each re-anchor the clock
  if (const auto t = trackTime();
      (state & SEEKING) == 0 && t && std::chrono::abs(*t - *pos) < 1s)
    return;
  seek(*pos);
}

// Playback started while another function is active: ask the head unit to
// activate us, as a disc insertion would. Nothing else should change the head
// unit's active function (e.g. (re)connection, change in playback config, ie
// repeat/shuffle, etc)
void CDChanger::receive(const Playback::Playing &p) {
  if (sourcePlaying == p.playing)
    return;
  rebaseTime();
  sourcePlaying = p.playing;
  if (p.playing && !active)
    notify(condition, r_Ejection);
}

// On disconnect nothing reports anymore; fall back to the free-running clock
void CDChanger::receive(const Playback::Connected &c) {
  if (c.connected)
    return;
  rebaseTime();
  sourcePlaying = std::nullopt;
  if (track != 0)
    return;
  track = 1;
  seek(0ms);
}

} // namespace avclan
