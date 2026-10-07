// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <type_traits>

#include <btstack_tlv.h>

#include "btstack.h" // IWYU pragma: export

namespace avclan::bluetooth {

class Mobile;

using BdAddr = std::array<uint8_t, BD_ADDR_LEN>;
static_assert(sizeof(BdAddr) == BD_ADDR_LEN);

// Provides the expected UX for Bluetooth (re)connection behavior:
//  - Reconnect to most recently connected device
//  - Don't automatically reconnect following an intentional disconnect
//  - Reconnect to (eligible) paired devices in reverse recency
class Bonds {
public:
  explicit Bonds(Mobile &mobile) : mobile{mobile} {}

  // Needs HCI_STATE_WORKING: the link-key DB is keyed by our controller
  // address
  void load();
  bool is_empty() const { return count == 0; }
  void reconnect_start();

  // Fires for both an incoming (phone-initiated) ACL and our own reconnect
  void on_connected(hci_con_handle_t handle, BdAddr addr);
  void on_connect_failed(const BdAddr &addr);
  // BTstack has already stored (or declined to store) the key by the time the
  // app sees HCI_EVENT_LINK_KEY_NOTIFICATION
  bool on_link_key(BdAddr addr);
  // Only a phone-initiated disconnect suppresses auto-reconnect; drops
  // (timeout / out of range) keep the phone for the next boot.
  void on_disconnected(hci_con_handle_t handle, uint8_t reason);

  void forget_all();

private:
  static constexpr uint32_t TLV_TAG = 0x4D4F434B; // 'MOCK'
  // Longest a reconnect attempt pages an absent phone
  static constexpr uint32_t PAGE_TIMEOUT_MS = 10240;
  // Must outlast a page attempt
  static constexpr uint32_t RECONNECT_GUARD_MS = PAGE_TIMEOUT_MS + 2000;

  struct Entry {
    BdAddr addr;
    bool auto_reconnect;
  };
  struct Stored {
    static constexpr uint8_t VERSION = 2;
    uint8_t version = VERSION;
    std::array<Entry, NVM_NUM_LINK_KEYS> entries{};
  };
  static_assert(std::is_trivially_copyable_v<Stored>);
  // entries is saved as a variable-length tail, so it must be last
  static_assert(sizeof(Stored) ==
                offsetof(Stored, entries) + sizeof(Stored::entries));

  static bool has_link_key(BdAddr &addr);

  std::span<Entry> active() { return {stored.entries.data(), count}; }
  void save();
  // When full, a new phone overwrites back(), the bond BTstack just evicted
  void promote(const BdAddr &addr);
  void reconnect_stop();
  void reconnect_try_next();

  Mobile &mobile;

  Stored stored{};
  uint8_t count = 0;
  const btstack_tlv_t *tlv = nullptr;
  void *tlv_context = nullptr;

  std::optional<uint8_t> reconnect_idx; // set while the boot sequence runs
  btstack_timer_source_t reconnect_guard_timer{
      // static only to avoid a -fanalyzer false positive: GCC's
      // function-pointer thunk calls a non-static operator() with a null this
      .process =
          [](btstack_timer_source_t *ts) static {
            auto &self =
                *static_cast<Bonds *>(btstack_run_loop_get_timer_context(ts));
            puts("reconnect attempt timed out; advancing to next device");
            ++*self.reconnect_idx;
            self.reconnect_try_next();
          },
      .context = this,
  };

  // The currently-connected ACL, tracked so a disconnect (which reports only
  // a connection handle, not an address) can be mapped back to a device.
  // Classic A2DP sink talks to one source at a time, so a single slot
  // suffices.
  BdAddr current_peer{};
  hci_con_handle_t current_handle = HCI_CON_HANDLE_INVALID;
};

} // namespace avclan::bluetooth
