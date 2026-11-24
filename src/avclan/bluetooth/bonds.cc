// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <btstack_tlv.h>

#include "btstack.h" // IWYU pragma: export

#include "bonds.hpp"
#include "mobile.hpp"

namespace avclan::bluetooth {

void Bonds::load() {
  btstack_tlv_get_instance(&tlv, &tlv_context);
  count = 0;
  if (tlv == nullptr)
    return;
  int len = tlv->get_tag(tlv_context, TLV_TAG,
                         reinterpret_cast<uint8_t *>(&stored), sizeof(stored));
  if (len > 0 && stored.version == Stored::VERSION)
    count = static_cast<uint8_t>(
        (static_cast<size_t>(len) - offsetof(Stored, entries)) / sizeof(Entry));
  stored.version = Stored::VERSION;

  // Drop phones whose bond was deleted since the last save
  auto stale = std::ranges::remove_if(
      active(), [](Entry &entry) { return !has_link_key(entry.addr); });
  count -= static_cast<uint8_t>(stale.size());
  bool changed = !stale.empty();

  // Adopt bonds stored without a matching save (power loss, older format)
  if (btstack_link_key_iterator_t iter;
      gap_link_key_iterator_init(&iter) != 0) {
    BdAddr addr;
    link_key_t key;
    link_key_type_t type;
    while (gap_link_key_iterator_get_next(&iter, addr.data(), key, &type) !=
           0) {
      if (count < stored.entries.size() &&
          std::ranges::find(active(), addr, &Entry::addr) == active().end()) {
        stored.entries[count++] = {.addr = addr, .auto_reconnect = true};
        changed = true;
      }
    }
    gap_link_key_iterator_done(&iter);
  }
  if (changed)
    save();
}

void Bonds::reconnect_start() {
  if (std::ranges::none_of(active(), &Entry::auto_reconnect)) {
    puts("no phone to reconnect (all deliberately disconnected); waiting for "
         "a phone-initiated connect");
    return;
  }
  gap_set_page_timeout(PAGE_TIMEOUT_MS * 8 / 5); // 0.625 ms units
  reconnect_idx = 0;
  reconnect_try_next();
}

void Bonds::on_connection_request(const BdAddr &addr) { requester = addr; }

void Bonds::on_connected(hci_con_handle_t handle, BdAddr addr) {
  const bool phone_initiated = requester == addr;
  requester.reset();
  if (current_handle == HCI_CON_HANDLE_INVALID) {
    adopt(handle, addr);
  } else if (phone_initiated) {
    newcomer_handle = handle;
    newcomer_peer = addr;
  } else {
    printf("dropping late reconnect to %s; another phone is connected\n",
           bd_addr_to_str(addr.data()));
    gap_disconnect(handle);
  }
}

void Bonds::on_connect_failed(const BdAddr &addr) {
  if (requester == addr)
    requester.reset();
  if (!reconnect_idx || addr != active()[*reconnect_idx].addr)
    return;
  ++*reconnect_idx;
  reconnect_try_next();
}

// The old phone's disconnect then arrives for a handle that is no longer
// current, so it keeps its auto-reconnect
void Bonds::on_encrypted(hci_con_handle_t handle) {
  if (handle != newcomer_handle)
    return;
  // bd_addr_to_str() returns one shared buffer: one address per printf
  printf("%s takes over from ", bd_addr_to_str(newcomer_peer.data()));
  printf("%s\n", bd_addr_to_str(current_peer.data()));
  gap_disconnect(current_handle);
  newcomer_handle = HCI_CON_HANDLE_INVALID;
  adopt(handle, newcomer_peer);
}

bool Bonds::on_link_key(BdAddr addr) {
  if (!has_link_key(addr))
    return false;
  promote(addr);
  return true;
}

void Bonds::on_disconnected(hci_con_handle_t handle, uint8_t reason) {
  if (handle == newcomer_handle)
    newcomer_handle = HCI_CON_HANDLE_INVALID;
  if (handle != current_handle)
    return;
  current_handle = HCI_CON_HANDLE_INVALID;
  // TODO: phones also send 0x13 for BT-off and for dropping an ACL with no
  // profiles; ignoring 0x13 unless a profile was up would exclude the latter.
  if (reason != ERROR_CODE_REMOTE_USER_TERMINATED_CONNECTION)
    return;
  auto active = this->active();
  auto pos = std::ranges::find(active, current_peer, &Entry::addr);
  if (pos == active.end() || !pos->auto_reconnect)
    return;
  printf("deliberate disconnect; suppressing auto-reconnect for %s\n",
         bd_addr_to_str(current_peer.data()));
  pos->auto_reconnect = false;
  save();
}

void Bonds::forget_all() {
  reconnect_stop();
  if (current_handle != HCI_CON_HANDLE_INVALID)
    gap_disconnect(current_handle);
  gap_delete_all_link_keys();
  count = 0;
  if (tlv != nullptr)
    tlv->delete_tag(tlv_context, TLV_TAG);
  puts("forgot all pairings");
}

bool Bonds::has_link_key(BdAddr &addr) {
  link_key_t key;
  link_key_type_t type;
  return gap_get_link_key_for_bd_addr(addr.data(), key, &type);
}

void Bonds::adopt(hci_con_handle_t handle, BdAddr addr) {
  current_handle = handle;
  current_peer = addr;
  reconnect_stop();

  // Re-storing the key refreshes its eviction age to match promote()
  link_key_t key;
  link_key_type_t type;
  if (!gap_get_link_key_for_bd_addr(addr.data(), key, &type))
    return;
  gap_store_link_key_for_bd_addr(addr.data(), key, type);
  promote(addr);
}

void Bonds::save() {
  if (tlv == nullptr)
    return;
  if (tlv->store_tag(tlv_context, TLV_TAG,
                     reinterpret_cast<const uint8_t *>(&stored),
                     offsetof(Stored, entries) + (count * sizeof(Entry))) != 0)
    puts("failed to save bond order");
}

void Bonds::promote(const BdAddr &addr) {
  auto active = this->active();
  auto pos = std::ranges::find(active, addr, &Entry::addr);
  if (pos == active.begin() && pos != active.end() && pos->auto_reconnect)
    return;
  if (pos == active.end()) {
    if (count < stored.entries.size())
      count++;
    active = this->active();
    pos = active.end() - 1;
    pos->addr = addr;
  }
  pos->auto_reconnect = true;
  std::rotate(active.begin(), pos, pos + 1);
  save();
}

void Bonds::reconnect_stop() {
  reconnect_idx.reset();
  btstack_run_loop_remove_timer(&reconnect_guard_timer);
}

void Bonds::reconnect_try_next() {
  btstack_run_loop_remove_timer(&reconnect_guard_timer);
  auto active = this->active();
  while (*reconnect_idx < count && !active[*reconnect_idx].auto_reconnect)
    ++*reconnect_idx;
  if (*reconnect_idx >= count) {
    reconnect_idx.reset();
    puts("no bonded device reachable; staying connectable");
    return;
  }
  BdAddr &addr = active[*reconnect_idx].addr;
  printf("reconnect attempt %u/%u: %s\n", *reconnect_idx + 1, count,
         bd_addr_to_str(addr.data()));

  mobile.connect(addr.data());

  // Guard against an attempt that never reports a connection result (e.g.
  // the connect failed to start) so the queue can't stall.
  btstack_run_loop_set_timer(&reconnect_guard_timer, RECONNECT_GUARD_MS);
  btstack_run_loop_add_timer(&reconnect_guard_timer);
}

} // namespace avclan::bluetooth
