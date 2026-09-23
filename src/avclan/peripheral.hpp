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
  static_assert(sizeof...(Devs) <= 0x100,
                "too many devices: Notifier packs the device index into 8 bits");

  static constexpr UBaseType_t EMIT_QUEUE_LEN = 2 * sizeof...(Devs);

  Peripheral(Bus &bus, uint16_t address)
      : bus{bus}, address_{address},
        emit_requests_{xQueueCreate(EMIT_QUEUE_LEN, sizeof(uint32_t))} {
    configASSERT(emit_requests_);
    bus.init(address);
    (std::get<Devs>(devices_).init(Notifier{emit_requests_, index_of<Devs>()}),
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
      return unexpected{detail::SendError{.owning_device = out->owning_device,
                                          .reaction = out->reaction,
                                          .err = err}};

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
    static const uint8_t lancheck_resp[] = {0x00, to_underlying(COMM_CTRL),
                                            to_underlying(LAN), 0xFF, 0xFF};

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
        case PACK3(COMMUNICATION_V1, COMM_CTRL,
                   to_underlying(Advertise_Function)):
        case PACK3(COMMUNICATION_V2, COMM_CTRL,
                   to_underlying(Advertise_Function)): {
          auto enable_d = [](auto &d, auto &out) { d.enable(out); };
          ((Devs::id == static_cast<Device>(b3)
                ? originate(std::get<Devs>(devices_), *out, enable_d)
                : void()),
           ...);
          break;
        }
        case PACK3(COMMUNICATION_V1, COMM_CTRL, to_underlying(Ping_Req)):
        case PACK3(COMMUNICATION_V2, COMM_CTRL, to_underlying(Ping_Req)): {
          out->is_unicast = true;
          const uint8_t ping_resp[] = {0x00, to_underlying(COMM_CTRL),
                                       from, to_underlying(Ping_Resp),
                                       0xFF, b3};
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
              0x00, to_underlying(COMM_CTRL), from,
              to_underlying(List_Functions_Resp), to_underlying(Devs::id)...};
          out->length = sizeof(list_functions_resp);
          memcpy(out->data, list_functions_resp, sizeof(list_functions_resp));
          out->reaction = 1;
          break;
        }
        // case Restart_Lan: not handled
        default: break;
      }
    } else if (in.peripheral_addr == address_ && b0 == 0x00) {
      auto handle_d = [&](auto &d, auto &out) { d.handle(in, out); };
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

  // Blocks until a device requests an emit; requests are served in order.
  std::unique_ptr<Frame> poll() {
    std::unique_ptr<Frame> out = Frame::acquire();
    if (!out) {
      puts("!! failed Frame alloc in poll !!");
      return {};
    }

    uint32_t val;
    xQueueReceive(emit_requests_, &val, portMAX_DELAY);
    const uint8_t index = val & 0xFF;

    auto emit_d = [val](auto &d, auto &out) { d.emit(out, val >> 8); };
    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
      ((Is == index ? originate(std::get<Is>(devices_), *out, emit_d) : void()),
       ...);
    }(std::index_sequence_for<Devs...>{});
    return out;
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
  QueueHandle_t emit_requests_;
};
} // namespace avclan
