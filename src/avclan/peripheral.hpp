// copyright (C) 2006 Marcin Slonicki <marcin@softservice.com.pl>
// copyright (C) 2007 Louis Frigon
// Copyright (C) 2015 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

#include "FreeRTOS.h" // IWYU pragma: export
#include "queue.h"

#include "avclan.h"
#include "bus.hpp"
#include "device.hpp"
#include "frame.hpp"
#include "stdshim.hpp"

namespace avclan {
namespace detail {
enum class Party : uint8_t { Sender, Recipient };
}

template <DeviceInterface... Devs> class Peripheral {
  using Party = detail::Party;
  using enum Party;

public:
  using Error = detail::Error;

  // lack of static reflection until C++26 limits our methods of doing a
  // compile-time collision check. Within Peripheral, we can at least assert no
  // collision with Devices *used in a particular instantiation* but it is not a
  // universal check against all Device enum values
  static_assert(((Devs::id != NoDevice) && ...),
                "a registered Device id collides with the NoDevice sentinel; "
                "update the sentinel value in avclan.h");
  static_assert(
      sizeof...(Devs) <= 0x100,
      "too many devices: Notifier packs the device index into 8 bits");

  // A device queues each condition at most once, so the condition queue is
  // never full when a condition is notified
  // Min length of 1 required by FreeRTOS/xQueueCreate
  static constexpr UBaseType_t CONDITION_QUEUE_LEN =
      std::max<UBaseType_t>(1, (0 + ... + Devs::conditions));
  static constexpr UBaseType_t EVENT_QUEUE_LEN =
      std::max<UBaseType_t>(1, (0 + ... + Devs::reserve_events));

  Peripheral(Bus &bus, uint16_t address)
      : bus{bus}, address_{address},
        conditions_{xQueueCreate(CONDITION_QUEUE_LEN, sizeof(uint32_t))},
        events_{xQueueCreate(EVENT_QUEUE_LEN, sizeof(uint32_t))},
        notifications_{xQueueCreateSet(CONDITION_QUEUE_LEN + EVENT_QUEUE_LEN)} {
    configASSERT(conditions_ && events_ && notifications_);
    xQueueAddToSet(conditions_, notifications_);
    xQueueAddToSet(events_, notifications_);
    bus.init(address);
    (std::get<Devs>(devices_).init(
         NotifyQueues{conditions_, events_, index_of<Devs>()}),
     ...);
  }
  Peripheral(const Peripheral &) = delete;

  uint16_t controller() const { return controller_; };
  template <DeviceInterface Dev> Dev &device() {
    return std::get<Dev>(devices_);
  }

  void mute(bool mute) { bus.mute(mute); };
  bool is_muted() const { return bus.is_muted(); };

#ifndef NDEBUG
  Bus &get_bus() { return bus; }
  void set_controller(uint16_t addr) { controller_ = addr; }
#endif

  expected<std::unique_ptr<Frame>, Error::Read>
  read(Frame::Print print = Frame::Print{}) {
    return bus.read(print);
  };

  expected<std::unique_ptr<Frame>, detail::SendError>
  send(std::unique_ptr<Frame> out, Frame::Print print = Frame::Print{}) {
    // To "forge" a controller_addr, instantiate a new/different Peripheral
    stamp<Sender>(*out);
    out->control = 0xF;
    auto err = bus.send(*out, print);
    if (err != Error::Send{0})
      return unexpected{detail::SendError{
          .owning_device = out->owning_device,
          .reaction = out->reaction,
          .err = err,
      }};

    return out;
  };

#define PACK3(a, b, c) (((uint32_t)(a) << 16) | ((uint32_t)(b) << 8) | (c))

  // expected needed to distinguish don't vs can't respond
  expected<std::unique_ptr<Frame>, Error::Read> route(const Frame &in) {
    using enum Device;
    using enum Action;

    if (is_muted() || in.length < 3)
      return {};

    std::unique_ptr<Frame> out = Frame::acquire();
    if (!out) {
      puts("!! failed Frame alloc in route !!");
      return unexpected{Error::Read::POOL_EMPTY};
    }

    // 0xFF placeholders are variant bytes filled by writing directly to
    // out->data[N] after memcpy.
    static const uint8_t lancheck_resp[] = {
        0x00, to_underlying(COMM_CTRL), to_underlying(LAN), 0xFF, 0xFF,
    };

    stamp<Recipient>(*out);

    const uint8_t *data = in.data;
    const uint8_t b0 = *data++;
    const uint8_t b1 = *data++;
    const uint8_t b2 = *data++;

    // the shortest known/valid messages are 3 bytes long
    const uint8_t b3 = (in.length > 3) ? *data++ : 0;

    if (!in.is_unicast) {
      const auto from = b0;
      const auto to = b1;
      const auto action = b2;
      // Broadcast: bytes are (from, to, action, [extra...]).
      // peripheral_addr unchecked — always 0xFFF or 0x1FF in known traffic.
      switch (PACK3(from, to, action)) {
        case PACK3(LAN, COMM_CTRL, to_underlying(Lancheck_Scan_Req)):
          out->length = sizeof(lancheck_resp);
          out->is_unicast = true;
          memcpy(out->data, lancheck_resp, sizeof(lancheck_resp));
          out->data[3] = to_underlying(Lancheck_Scan_Resp);
          out->data[4] = 0x01;
          out->reaction = 1;
          break;
        case PACK3(LAN, COMM_CTRL, to_underlying(Lancheck_Req)):
          out->length = sizeof(lancheck_resp);
          out->is_unicast = true;
          memcpy(out->data, lancheck_resp, sizeof(lancheck_resp));
          out->data[3] = to_underlying(Lancheck_Resp);
          out->data[4] = 0x00;
          out->reaction = 1;
          break;
        case PACK3(LAN, COMM_CTRL, to_underlying(Lancheck_End_Req)):
          out->is_unicast = true;
          out->length = sizeof(lancheck_resp) - 1;
          memcpy(out->data, lancheck_resp, out->length);
          out->data[3] = to_underlying(Lancheck_End_Resp);
          out->reaction = 1;
          break;
        case PACK3(COMMUNICATION_V1, COMM_CTRL, to_underlying(Active_Function)):
        case PACK3(COMMUNICATION_V2, COMM_CTRL,
                   to_underlying(Active_Function)): {
          auto reported_active_d = [](auto &d, auto &out) {
            if constexpr (requires { d.reported_active(out); })
              d.reported_active(out);
          };
          ((Devs::id == static_cast<Device>(b3)
                ? originate(std::get<Devs>(devices_), *out, reported_active_d)
                : void()),
           ...);
          break;
        }
        case PACK3(COMMUNICATION_V1, COMM_CTRL, to_underlying(Ping_Req)):
        case PACK3(COMMUNICATION_V2, COMM_CTRL, to_underlying(Ping_Req)): {
          out->is_unicast = true;
          const uint8_t ping_resp[] = {
              0x00, to_underlying(COMM_CTRL),
              from, to_underlying(Ping_Resp),
              0xFF, b3,
          };
          out->length = sizeof(ping_resp);
          memcpy(out->data, ping_resp, sizeof(ping_resp));
          out->reaction = 1;
          break;
        }
        case PACK3(COMMUNICATION_V1, COMM_CTRL,
                   to_underlying(List_Functions_Req)):
        case PACK3(COMMUNICATION_V2, COMM_CTRL,
                   to_underlying(List_Functions_Req)): {
          controller_ = in.controller_addr;
          stamp<Recipient>(*out); // re-stamp now that controller_ is known
          out->is_unicast = true;
          const uint8_t list_functions_resp[] = {
              0x00,
              to_underlying(COMM_CTRL),
              from,
              to_underlying(List_Functions_Resp),
          };
          out->length = sizeof(list_functions_resp);
          memcpy(out->data, list_functions_resp, sizeof(list_functions_resp));
          ((is_listed<Devs>
                ? void(out->data[out->length++] = to_underlying(Devs::id))
                : void()),
           ...);
          out->reaction = 1;
          break;
        }
        // case Restart_Lan: not handled
        default: break;
      }
    } else if (in.peripheral_addr == address_ && b0 == 0x00) {
      auto handle_d = [&](auto &d, auto &out) {
        if constexpr (requires { d.handle(in, out); })
          d.handle(in, out);
      };
      ((Devs::id == static_cast<Device>(b2)
            ? originate(std::get<Devs>(devices_), *out, handle_d)
            : void()),
       ...);
    }

    if (out->reaction > 0)
      return out;

    return {};
  }

#undef PACK3

  std::unique_ptr<Frame>
  react(expected<std::unique_ptr<Frame>, detail::SendError> exp) {
    const Device from =
        exp ? exp.value()->owning_device : exp.error().owning_device;

    std::unique_ptr<Frame> next;
    ((Devs::id == from &&
      (next = std::get<Devs>(devices_).react(std::move(exp)))) ||
     ...);
    return next;
  }

  // Blocks until a device notifies; notifications are served in order. `emit`
  // runs the device's deferred work and optionally fills a frame;
  // otherwise returns null.
  std::unique_ptr<Frame> poll() {
    uint32_t item;
    QueueSetMemberHandle_t from =
        xQueueSelectFromSet(notifications_, portMAX_DELAY);
    xQueueReceive(from, &item, 0);
    const uint8_t id = item & 0xFF;

    std::unique_ptr<Frame> out = Frame::acquire();
    if (!out) {
      puts("!! failed Frame alloc in poll; waiting !!");
      do
        out = Frame::acquire();
      while (!out);
    }

    auto emit_d = [&](auto &d, auto &out) {
      if (from == conditions_)
        d.emit(out, condition, item >> 8);
      else
        d.emit(out, event, item >> 8);
    };
    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
      ((Is == id ? originate(std::get<Is>(devices_), *out, emit_d) : void()),
       ...);
    }(std::index_sequence_for<Devs...>{});

    if (out->reaction > 0)
      return out;

    return {};
  }

private:
  // Dev's position in Devs
  template <class Dev> static constexpr uint8_t index_of() {
    constexpr std::array is_dev{std::is_same_v<Dev, Devs>...};
    return std::ranges::find(is_dev, true) - is_dev.begin();
  }

  template <Party P> void stamp(Frame &out) const {
    if constexpr (P == Sender)
      out.controller_addr = address_;
    else
      out.peripheral_addr = controller_;
  }

  void originate(DeviceInterface auto &dev, Frame &out, auto &&fill) {
    out.owning_device = std::remove_reference_t<decltype(dev)>::id;
    stamp<Recipient>(out); // default set FIRST; fill() may override
    fill(dev, out);
  }

  Bus &bus;
  uint16_t controller_ = 0;
  const uint16_t address_;
  std::tuple<Devs...> devices_;
  QueueHandle_t conditions_;
  QueueHandle_t events_;
  QueueSetHandle_t notifications_; // both queues, in arrival order
};
} // namespace avclan
