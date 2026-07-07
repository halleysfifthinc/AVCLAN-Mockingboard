// copyright (C) 2006 Marcin Slonicki <marcin@softservice.com.pl>
// copyright (C) 2007 Louis Frigon
// Copyright (C) 2015 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "avclan.h"
#include "bus.hpp"
#include "device.hpp"
#include "frame.hpp"

#include <cstdint>
#include <cstring>
#include <tuple>

namespace avclan {
template <DeviceInterface... Devs> class Peripheral {
public:
  using Error = detail::Error;

  Peripheral(Bus bus, uint16_t address) : bus{bus}, address_{address} {
    bus.init();
    (std::get<Devs>(devices_).init(), ...);
  }

  uint16_t address() const { return address_; };
  uint16_t controller() const { return controller_; };
  void mute(bool mute) { bus.mute(mute); };
  bool is_muted() const { return bus.is_muted(); };

  Error::Read read(Frame *in, Frame::Print print) {
    return bus.read(address_, in, print);
  };
  // To "forge" a controller_addr, instantiate a new/different Peripheral
  Error::Send send(Frame *out, Frame::Print print) {
    postmark(out);
    return bus.send(out, print);
  };

#define PACK3(a, b, c) (((uint32_t)(a) << 16) | ((uint32_t)(b) << 8) | (c))

  void route(const Frame *in, Frame *out) {
    using enum Device;
    using enum Action;
    out->reaction = 0;

    if (AVCLAN_ismuted() || in->length < 3)
      return;

    // 0xFF placeholders are variant bytes filled by writing directly to
    // out->data[N] after memcpy.
    static const uint8_t lancheck_resp[] = {0x00, to_underlying(COMM_CTRL),
                                            to_underlying(LAN), 0xFF, 0xFF};

    out->peripheral_addr = controller_;

    const uint8_t *data = in->data;
    const uint8_t b0 = *data++;
    const uint8_t b1 = *data++;
    const uint8_t b2 = *data++;
    uint8_t b3 = 0;
    if (in->length > 3) // the shortest known/valid messages are 3 bytes long
      b3 = *data++;

    if (!in->is_unicast) {
      // Broadcast: bytes are (from, to, action, [extra...]).
      // peripheral_addr unchecked — always 0xFFF or 0x1FF in known traffic.
      switch (PACK3(b0, b1, b2)) {
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
                   to_underlying(Advertise_Function)):
          ((Devs::id == static_cast<Device>(b3)
            ? std::get<Devs>(devices_).enable(out),
            0 : 0),
           ...);
          break;
        case PACK3(COMMUNICATION_V1, COMM_CTRL, to_underlying(Ping_Req)):
        case PACK3(COMMUNICATION_V2, COMM_CTRL, to_underlying(Ping_Req)): {
          out->is_unicast = true;
          const uint8_t ping_resp[] = {0x00,
                                       to_underlying(COMM_CTRL),
                                       to_underlying(COMMUNICATION_V1),
                                       to_underlying(Ping_Resp),
                                       0xFF,
                                       b3};
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
          out->peripheral_addr = controller_;
          out->is_unicast = true;
          const uint8_t list_functions_resp[] = {
              0x00, to_underlying(COMM_CTRL), to_underlying(COMMUNICATION_V1),
              to_underlying(List_Functions_Resp), to_underlying(CD_CHANGER)};
          out->length = sizeof(list_functions_resp);
          memcpy(out->data, list_functions_resp, sizeof(list_functions_resp));
          out->reaction = 1;
          break;
        }
          // case Restart_Lan: not handled
      }
    } else if (in->peripheral_addr == address_ && b0 == 0x00) {
      ((Devs::id == static_cast<Device>(b2)
        ? device_preroute(std::get<Devs>(devices_), in, out),
        0 : 0),
       ...);
    }
  }

  void react(Frame *out, Error::Send err) {
    if (((Devs::id == out->owning_device) || ...))
      ((Devs::id == out->owning_device
        ? std::get<Devs>(devices_).react(out, err),
        0 : 0),
       ...);
    else
      out->reaction = 0;
  }

#undef PACK3

  template <class F> void poll_devices(F &&fun) {
    (poller(std::get<Devs>(devices_), fun), ...);
  }

private:
  void postmark(Frame *out) const {
    out->controller_addr = address_;
    out->control = 0xF;
  }

  template <DeviceInterface Dev, class F> void poller(Dev &dev, F &&fun) {
    if (dev.pending() && fun(dev))
      dev.resolvepending();
  }
  template <DeviceInterface Dev>
  void device_preroute(Dev &dev, const Frame *in, Frame *out) {
    out->owning_device = Dev::id;
    dev.handle(in, out);
  }

  Bus bus;
  uint16_t controller_ = 0;
  const uint16_t address_;
  std::tuple<Devs...> devices_;
};
} // namespace avclan
