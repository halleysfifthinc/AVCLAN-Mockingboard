// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Media bindings are (very) loosely modeled after the model-view-controller
// (MVC) design paradigm.
// The head-unit and now Bluetooth AVRCP/A2DP internally store functionally
// overlapping state (e.g. track time), and both have "controller" aspects: each
// can initiate changes to their internal state, but need to communicate the
// updated state to stay in sync.

// Terms:
// - Model: the originating authority for state/data
// - View: displays state and asks the model for changes.
// - Endpoint: a class that speaks as a model or view (i.e. to the HU via an
//   AVC-LAN device, or to the phone via Bluetooth).
// - Domain: a given set of state shared between two endpoints
// - Request: from the view endpoint to the model endpoint; asks for a change.
// - Report: from the model endpoint to the view endpoint; tells of a change.
// - Binding: connects one domain's requests and reports between its two
//   endpoints. An endpoint with no binding works alone.
//
// Call/data flow:
//
//   [sender's context; may hold its own locks]
//   S::send(msg)                    no-op if unbound
//   `-> Inbox(msg)
//       `-> R::deliver(msg)         never waits on the receiver
//           |-> mailbox.push(msg)   replaces a pending msg of its type
//           `-> R::wake(msg)        R decides if msg needs a prompt drain;
//                 |                 must not block
//   [receiver's context]
//   R::drain()  <-'                 in last-write order
//   `-> R::receive(alternative)     free to take R's own locks

#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <cstdint>
#include <optional>
#include <variant>

#include "FreeRTOS.h" // IWYU pragma: export
#include "task.h"

namespace avclan::media {

template <class T> constexpr bool is_variant = false;
template <class... Ts> constexpr bool is_variant<std::variant<Ts...>> = true;

// A domain lists the set of reportable/requestable changes in overlapping state
// between the model (authoritative state source) and view (locally represents
// state). Domain's must define non-overlapping std::variants of Requests and
// Reports. Only shared state needs to be handled by Request/Report
// alternatives.
//
// Messages communicate the intended state being requested or reported. Only
// the newest requested/reported state is kept when multiple of the same message
// type are received before being drained. Incremental changes are only allowed
// if dropping one (or more) is harmless (e.g. multiple button presses
// appearing as one is fine).
//
// (Domain implementations are modelled as tagged unions; individual message
// types will typically be defined in the domain's namespace, alongside the
// Request and Report variants.)
template <class D>
concept Domain =
    is_variant<typename D::Request> && is_variant<typename D::Report> &&
    !std::same_as<typename D::Request, typename D::Report>;

template <class E, class Message>
concept Receives =
    requires(E &endpoint, const Message &msg) { endpoint.receive(msg); };

// Connects view requests to the model, and model reports to the view. One model
// and one view must `attach` to each binding to send/receive messages.
// Either endpoint may be silent (e.g. a view without support for interaction
// doesn't need to send requests to the model).
template <Domain D> struct Binding {

  // Indirection for endpoint type-erasure
  template <class Message> struct Inbox {
    void (*deliver)(void *endpoint, const Message &) = nullptr;
    void *endpoint = nullptr;

    void operator()(const Message &msg) const {
      if (deliver != nullptr)
        deliver(endpoint, msg);
    }
  };

  Inbox<typename D::Request> model;
  Inbox<typename D::Report> view;

  template <class Message> auto &inbox() {
    if constexpr (std::same_as<Message, typename D::Request>)
      return model;
    else
      return view;
  }
};

// Receives `In` requests and sends `Out` reports. `Derived` provides:
// - a public `receive(const M &)` for each alternative M of `In`. Ignore
//   specific alternatives using empty overloads (catch-all `receive(const
//   auto&)` methods are not recommended).
// - `wake(const In &)`, which must be non-blocking. It must eventually trigger
//   a `drain()` in the endpoint's context.
//
// A report must never cause a request.
template <class Derived, Domain D, class In, class Out> class Endpoint {
public:
  // Attach one end of a Binding to this model/view.
  // Unsafe to call when the scheduler is running due to non-atomic writes that
  // could allow racy reads in `send`
  void attach(Binding<D> &b) {
    binding = &b;
    b.template inbox<In>() = {
        .deliver =
            [](void *self, const In &msg) static {
              static_cast<Endpoint *>(self)->deliver(msg);
            },
        .endpoint = this,
    };
  }

  void deliver(const In &msg) {
    mailbox.push(msg);
    static_cast<Derived &>(*this).wake(msg);
  }

protected:
  // On the endpoint's own context. Returns true if any message was received.
  bool drain() {
    bool received = false;
    while (const auto msg = mailbox.pop()) {
      received = true;
      std::visit(
          [this]<class M>(const M &alternative) {
            static_assert(Receives<Derived, M>,
                          "endpoint has no public receive() for this message");
            static_cast<Derived &>(*this).receive(alternative);
          },
          *msg);
    }
    return received;
  }

  // Send a message (request/report) to the corresponding other endpoint in the
  // domain
  void send(const Out &msg) {
    if (binding != nullptr)
      binding->template inbox<Out>()(msg);
  }

private:
  template <class Message> class Mailbox {};

  // Holds the latest pending message of each type, so it never fills. A push
  // replaces a pending message of its type and moves it to the back. Callable
  // from any task; holds a critical section briefly and never waits on another
  // task.
  template <class... Ts> class Mailbox<std::variant<Ts...>> {
    using Message = std::variant<Ts...>;

  public:
    void push(const Message &msg) {
      const auto type = static_cast<uint8_t>(msg.index());
      taskENTER_CRITICAL();
      slots[type] = msg;
      const auto end = order.begin() + count;
      if (const auto pending = std::find(order.begin(), end, type);
          pending != end)
        std::rotate(pending, pending + 1, end);
      else
        order[count++] = type;
      taskEXIT_CRITICAL();
    }

    std::optional<Message> pop() {
      std::optional<Message> msg;
      taskENTER_CRITICAL();
      if (count > 0) {
        msg = slots[order[0]];
        std::shift_left(order.begin(), order.begin() + count, 1);
        --count;
      }
      taskEXIT_CRITICAL();
      return msg;
    }

  private:
    static constexpr auto types = std::variant_size_v<Message>;
    std::array<Message, types> slots{};
    std::array<uint8_t, types> order{}; // pending types, oldest write first
    uint8_t count = 0;
  };

  Mailbox<In> mailbox;
  Binding<D> *binding = nullptr;
};

template <class Derived, Domain D>
using Model = Endpoint<Derived, D, typename D::Request, typename D::Report>;
template <class Derived, Domain D>
using View = Endpoint<Derived, D, typename D::Report, typename D::Request>;

} // namespace avclan::media
