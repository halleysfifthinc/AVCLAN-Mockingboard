// copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <concepts>
#include <cstdint>
#include <memory>

#include "FreeRTOS.h" // IWYU pragma: export
#include "queue.h"

#include "avclan.h"
#include "frame.hpp"
#include "stdshim.hpp"

namespace avclan {

enum class Device : uint8_t {
  LAN = 0x00,
  COMM_CTRL = 0x01,
  COMMUNICATION_V1 = 0x11,
  COMMUNICATION_V2 = 0x12,
  SW_AUDIO = 0x21,
  SW_NAME = 0x23,
  SW_CONVERTING = 0x24,
  CMD_SW = 0x25,
  STATUS = 0x31,
  INFO_DISPLAY2 = 0x32,
  BEEP_HU = 0x28,
  BEEP_SPEAKERS = 0x29,
  FRONT_PSNG_MONITOR = 0x34,
  CD_CHANGER2 = 0x43,
  BLUETOOTH_TEL = 0x55,
  INFO_DRAWING = 0x56,
  NAV_ECU = 0x58,
  CAMERA = 0x5C,
  CLIMATE_DRAWING = 0x5D,
  AUDIO_DRAWING = 0x5E,
  TRIP_INFO_DRAWING = 0x5F,
  TUNER = 0x60,
  TAPE_DECK = 0x61,
  CD_SINGLE = 0x62,
  CD_CHANGER = 0x63,
  AUDIO_AMP = 0x74,
  GPS = 0x80,
  VOICE_CTRL = 0x85,
  XM_TUNER = 0xC0,
  CLIMATE_CTRL_DEV = 0xE0,
  TRIP_INFO = 0xE5,
};

// Notifies the Peripheral to emit for a device at `index`. Requests are served
// in order, one `emit()` each; coalescing is up to the device. `val` (24 bits)
// is device-defined. Returns whether the request was accepted within `wait`.
class Notifier {
public:
  Notifier() = default;
  Notifier(QueueHandle_t queue, uint8_t index) : queue{queue}, index{index} {}

  bool notify(uint32_t val = 0, TickType_t wait = portMAX_DELAY) const {
    const uint32_t item = (val << 8) | index;
    return xQueueSend(queue, &item, wait) == pdPASS;
  }

private:
  QueueHandle_t queue = nullptr;
  uint8_t index = 0;
};

template <class T>
concept DeviceInterface =
    requires { std::integral_constant<Device, T::id>{}; } &&
    requires(T dev, Notifier notifier, const Frame &in, Frame &out,
             expected<std::unique_ptr<Frame>, detail::SendError> exp,
             uint32_t payload) {
      dev.init(notifier);
      dev.handle(in, out);
      dev.enable(out);
      { dev.react(std::move(exp)) } -> std::same_as<std::unique_ptr<Frame>>;
      dev.emit(out, payload);
    };
} // namespace avclan
