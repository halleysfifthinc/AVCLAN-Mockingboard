// copyright (C) 2006 Marcin Slonicki <marcin@softservice.com.pl>
// copyright (C) 2007 Louis Frigon
// Copyright (C) 2015 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <tuple>
#include <utility>

#include "avclan.h"
#include "bus.hpp"
#include "device.hpp"
#include "frame.hpp"
#include "stdshim.hpp"

namespace avclan {

enum class Party : uint8_t { Sender, Recipient };

template <DeviceInterface... Devs> class Peripheral {
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

  Peripheral(Bus &bus, uint16_t address) : bus{bus}, address_{address} {
    bus.init();
    (std::get<Devs>(devices_).init(), ...);
  }

  uint16_t controller() const { return controller_; };
  template <DeviceInterface Dev> Dev &device() {
    return std::get<Dev>(devices_);
  }

  bool bus_is_active() const { return bus.is_active(); };
  void mute(bool mute) { bus.mute(mute); };
  bool is_muted() const { return bus.is_muted(); };

#ifndef NDEBUG
  Bus &get_bus() { return bus; }
#endif

  expected<std::unique_ptr<Frame>, Error::Read>
  read(Frame::Print print = Frame::Print{}) {
    return bus.read(address_, print);
  };

  expected<std::unique_ptr<Frame>, detail::SendError>
  send(std::unique_ptr<Frame> out, Frame::Print print = Frame::Print{}) {
    // To "forge" a controller_addr, instantiate a new/different Peripheral
    stamp<Sender>(out.get());
    out->control = 0xF;
    auto err = bus.send(out.get(), print);
    if (err != Error::Send{0})
      return unexpected{
          detail::SendError{out->owning_device, out->reaction, err}};

    return out;
  };

#define PACK3(a, b, c) (((uint32_t)(a) << 16) | ((uint32_t)(b) << 8) | (c))

  // expected needed to distinguish don't vs can't respond
  expected<std::unique_ptr<Frame>, Error::Read> route(const Frame *in) {
    using enum Device;
    using enum Action;

    if (is_muted() || in->length < 3)
      return {};

    std::unique_ptr<Frame> out(new (std::nothrow) Frame);
    if (!out) {
      puts("!! failed Frame alloc in route !!");
      return unexpected{Error::Read::POOL_EMPTY};
    }

    // 0xFF placeholders are variant bytes filled by writing directly to
    // out->data[N] after memcpy.
    static const uint8_t lancheck_resp[] = {0x00, to_underlying(COMM_CTRL),
                                            to_underlying(LAN), 0xFF, 0xFF};

    stamp<Recipient>(out.get());

    const uint8_t *data = in->data;
    const uint8_t b0 = *data++;
    const uint8_t b1 = *data++;
    const uint8_t b2 = *data++;
    uint8_t b3 = 0;
    if (in->length > 3) // the shortest known/valid messages are 3 bytes long
      b3 = *data++;

    if (!in->is_unicast) {
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
                ? originate(std::get<Devs>(devices_), out.get(), enable_d)
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
          controller_ = in->controller_addr;
          stamp<Recipient>(out.get()); // re-stamp now that controller_ is known
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
    } else if (in->peripheral_addr == address_ && b0 == 0x00) {
      auto handle_d = [&](auto &d, auto &out) { d.handle(in, out); };
      ((Devs::id == static_cast<Device>(b2)
            ? originate(std::get<Devs>(devices_), out.get(), handle_d)
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

  // Service ready devices in round-robin order
  std::unique_ptr<Frame> poll() {
    using U = std::unique_ptr<Frame>;
    auto does_emit = [&](DeviceInterface auto &dev) -> U {
      if (!dev.pending())
        return {};

      U out(new (std::nothrow) Frame);
      if (!out) {
        puts("!! failed Frame alloc in poll !!");
        return {};
      }

      originate(dev, out.get(), [](auto &d, auto &out) { d.emit(out); });
      return out;
    };

    // Runtime tuple index helper
    auto does_index_emit = [&](std::size_t t) -> U {
      return [&]<std::size_t... Is>(std::index_sequence<Is...>) -> U {
        U out;
        ((Is == t && (out = does_emit(std::get<Is>(devices_)))) || ...);
        return out;
      }(std::index_sequence_for<Devs...>{});
    };

    constexpr std::size_t N = sizeof...(Devs);
    if constexpr (N == 1) { // round-robin not needed
      return does_emit(std::get<0>(devices_));
    } else {
      static uint8_t rr_ = 0; // round-robin cursor
      const std::size_t start = rr_;
      for (std::size_t t = start; t < N; ++t) // [start, N)
        if (auto out = does_index_emit(t)) {
          rr_ = (t + 1 == N) ? 0 : t + 1;
          return out;
        }
      for (std::size_t t = 0; t < start; ++t) // [0, start); t+1 <= start < N
        if (auto out = does_index_emit(t)) {
          rr_ = t + 1;
          return out;
        }
      return {};
    }
  }

private:
  template <Party P> void stamp(Frame *out) const {
    if constexpr (P == Sender)
      out->controller_addr = address_;
    else
      out->peripheral_addr = controller_;
  }

  void originate(DeviceInterface auto &dev, Frame *out, auto &&fill) {
    out->owning_device = std::remove_reference_t<decltype(dev)>::id;
    stamp<Recipient>(out); // default set FIRST; fill() may override
    fill(dev, out);
  }

  Bus &bus;
  uint16_t controller_ = 0;
  const uint16_t address_;
  std::tuple<Devs...> devices_;
};
} // namespace avclan
