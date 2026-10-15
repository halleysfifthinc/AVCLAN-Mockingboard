// copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <concepts>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>

#include "FreeRTOS.h" // IWYU pragma: export
#include "queue.h"
#include "semphr.h"
#include "task.h"

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

// Notification kinds. An event's frame carries something specific to that
// notification, so every one is sent. A condition's frame is built from device
// state when emit() runs, so one queued notification covers every change before
// it.
struct notification_t {};
struct condition_t : notification_t {};
inline constexpr condition_t condition{};
struct event_t : notification_t {};
inline constexpr event_t event{};

// The Peripheral's notification queues, and the device's index among its
// devices
struct NotifyQueues {
  QueueHandle_t conditions = nullptr;
  QueueHandle_t events = nullptr;
  uint8_t id = 0;
};

// Notify the Peripheral of a condition or event to emit for the device.
// Conditions are "level-triggered" and may be notified >1 without blocking.
// Events are "edge-triggered" and may block, determined by the caller.
// `payload` is device-defined:
//  - Event payloads may use up to 24 bits
//  - Condition payloads must be below 32; a max of 32 condition types are
//  supported *per device*
// `Derived` provides `fill(Frame &out, Tag, uint32_t payload)` for each
// notification kind it uses; emit() calls it under `mutex`, so the bus tasks
// hold `mutex` while they touch device state.
template <class Derived> class Notifier {
public:
  void emit([[maybe_unused]] Frame &out, [[maybe_unused]] condition_t tag,
            [[maybe_unused]] uint32_t bit) {
    if constexpr (Derived::conditions > 0) {
      // Cleared before the lock, so a change during fill() notifies anew
      pending.fetch_and(~(1U << bit));
      const std::lock_guard lock(mutex);
      self().fill(out, tag, bit);
    }
  }
  void emit([[maybe_unused]] Frame &out, [[maybe_unused]] event_t tag,
            [[maybe_unused]] uint32_t payload) {
    if constexpr (Derived::reserve_events > 0) {
      const std::lock_guard lock(mutex);
      self().fill(out, tag, payload);
    }
  }

protected:
  void init(NotifyQueues q) { queues = q; }

  // Never blocks. Returns true if the notification was queued (false ==
  // condition was already queued).
  bool notify(condition_t /*tag*/, uint32_t bit) {
    static_assert(Derived::conditions > 0);
    configASSERT(bit < 32);
    const uint32_t mask = 1U << bit;
    if ((pending.fetch_or(mask) & mask) == 0) {
      const uint32_t item = (bit << 8) | queues.id;
      [[maybe_unused]] const bool queued =
          xQueueSend(queues.conditions, &item, 0) == pdPASS;
      configASSERT(queued);
      return queued; // aka `true`
    }
    return false;
  }

  // Returns true if the notification was queued within `wait`. Blocks by
  // default, so MUST NOT be called while holding the device lock.
  bool notify(event_t /*tag*/, uint32_t payload,
              TickType_t wait = portMAX_DELAY) {
    static_assert(Derived::reserve_events > 0);
    configASSERT(wait == 0 || xSemaphoreGetMutexHolder(mutex.native_handle()) !=
                                  xTaskGetCurrentTaskHandle());
    const uint32_t item = (payload << 8) | queues.id;
    return xQueueSend(queues.events, &item, wait) == pdPASS;
  }

  // (Mostly) Implements a std::mutex for an xSemaphore
  class xMutex {
  public:
    xMutex() : mutex_{xSemaphoreCreateMutex()} { configASSERT(mutex_); }
    xMutex(const xMutex &) = delete;
    // TODO: Needs a non-racey method to wait for no waiters
    // ~xMutex() { vSemaphoreDelete(mutex_); }

    void try_lock() noexcept { xSemaphoreTake(mutex_, 0); }
    void lock() noexcept { xSemaphoreTake(mutex_, portMAX_DELAY); }
    void unlock() noexcept { xSemaphoreGive(mutex_); }
    const SemaphoreHandle_t &native_handle() const { return mutex_; }

  private:
    SemaphoreHandle_t mutex_ = nullptr;
  };

  xMutex mutex;

private:
  Derived &self() { return static_cast<Derived &>(*this); }

  NotifyQueues queues;
  std::atomic<uint32_t> pending = 0; // bit `1 << val` per pending condition
};

// Trigger notifications after updating Device state
template <class Derived> class UpdateNotifier : public Notifier<Derived> {
protected:
  // Calls `mutate` while holding the device lock; it returns true if mutate
  // changed device state AND a new notification was queued. `wait` is passed
  // to an event's notify().
  template <class Tag, std::invocable F, class... Wait>
    requires((std::is_same_v<Tag, condition_t> ||
              std::is_same_v<Tag, event_t>) &&
             (std::is_same_v<uint32_t, std::invoke_result_t<F>> ||
              std::is_same_v<std::optional<uint32_t>, std::invoke_result_t<F>>))
  bool update(Tag tag, F &&mutate, Wait... wait) {
    std::optional<uint32_t> changed;
    {
      const std::lock_guard lock(this->mutex);
      changed = mutate();
    }
    if (changed)
      return this->notify(tag, *changed, wait...);

    return false;
  }
};

// Public base helper for devices that never notify
struct NonNotifier {
  static constexpr uint8_t conditions = 0;
  static constexpr uint8_t reserve_events = 0;
  // Shadow if an init implementation is still needed
  void init(NotifyQueues /*queues*/) {}
  // Unreachable: the queues were discarded, so nothing is queued
  void emit(Frame & /*out*/, notification_t /*tag*/, uint32_t /*payload*/) {}
};

// `conditions`: the number of unique conditions (max of 32)
// `reserve_events`: the number of event queue slots to reserve for the device;
// should be based on the expected device activity and to minimize blocking
// durations
template <class T>
concept DeviceInterface =
    requires { std::integral_constant<Device, T::id>{}; } &&
    requires { std::integral_constant<uint8_t, T::conditions>{}; } &&
    (T::conditions < 32) &&
    requires { std::integral_constant<uint8_t, T::reserve_events>{}; } &&
    requires(T dev, NotifyQueues queues, const Frame &in, Frame &out,
             expected<std::unique_ptr<Frame>, detail::SendError> exp,
             uint32_t payload) {
      dev.init(queues);
      dev.handle(in, out);
      dev.reported_active(out);
      { dev.react(std::move(exp)) } -> std::same_as<std::unique_ptr<Frame>>;
      dev.emit(out, condition, payload);
      dev.emit(out, event, payload);
    };
} // namespace avclan
