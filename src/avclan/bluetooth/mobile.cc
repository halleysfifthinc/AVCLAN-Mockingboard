// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdint>
#include <cstdio>

#include "FreeRTOS.h" // IWYU pragma: export

#include "btstack.h" // IWYU pragma: export
#if AVCLAN_BLUETOOTH_LOG
  #include "hci_dump.h"
  #include "hci_dump_embedded_stdout.h"
#endif

#include "bonds.hpp"
#include "mobile.hpp"
#include "platform.h"

namespace avclan::bluetooth {

Mobile::Mobile() {
  configASSERT(!self);
  self = this;
  platform::init([](void *ctx) static { static_cast<Mobile *>(ctx)->setup(); },
                 [](void *ctx) static { static_cast<Mobile *>(ctx)->work(); },
                 this);
}

#ifndef NDEBUG
void Mobile::forget_pairings() {
  forget_requested = true;
  platform::wake();
}
#endif

void Mobile::connect(bd_addr_t addr) { a2dp.connect(addr); }

// Runs once on BTstack's context
void Mobile::setup() {
#if AVCLAN_BLUETOOTH_LOG
  hci_dump_init(hci_dump_embedded_stdout_get_instance());
#endif

  l2cap_init();
  sdp_init();
#ifdef ENABLE_BLE
  // Initialize LE Security Manager. Needed for cross-transport key derivation
  sm_init();
#endif

  gap.setup();
  a2dp.setup();
  avrcp.setup();

  hci_add_event_handler(&hci_registration);

  puts("Mockingboard BT (A2DP sink + AVRCP) starting");

  hci_power_control(HCI_POWER_ON);
}

// Runs on BTstack's context after each platform::wake()
void Mobile::work() {
#ifndef NDEBUG
  if (forget_requested.exchange(false)) {
    bonds.forget_all();
    gap.enter_pairing();
  }
#endif

  avrcp.drain();
}

// Connect, pair, encrypt and disconnect events are all logged, for diagnosing
// connection issues
void Mobile::on_hci_event(uint8_t packet_type, uint16_t /* channel */,
                          uint8_t *packet, uint16_t /* size */) {
  if (packet_type != HCI_EVENT_PACKET)
    return;

  switch (hci_event_packet_get_type(packet)) {
    case BTSTACK_EVENT_STATE:
      if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
        bd_addr_t local;
        gap_local_bd_addr(local);
        printf("BTstack up. Local addr %s.\n", bd_addr_to_str(local));

        self->bonds.load();

        if (self->bonds.is_empty()) {
          puts("no bonded devices; opening initial pairing window");
          self->gap.enter_pairing();
        } else {
          gap_connectable_control(1);
          gap_discoverable_control(0);
          self->bonds.reconnect_start();
        }
      }
      break;
#ifndef NDEBUG
    case HCI_EVENT_SIMPLE_PAIRING_COMPLETE:
      printf("SSP complete, status 0x%02x\n",
             hci_event_simple_pairing_complete_get_status(packet));
      break;
    case HCI_EVENT_AUTHENTICATION_COMPLETE:
      printf("authentication complete, status 0x%02x\n",
             hci_event_authentication_complete_get_status(packet));
      break;
#endif
    // V2 shares V1's layout for these fields
    case HCI_EVENT_ENCRYPTION_CHANGE:
    case HCI_EVENT_ENCRYPTION_CHANGE_V2:
#ifndef NDEBUG
      printf("encryption change: enabled=%u\n",
             hci_event_encryption_change_get_encryption_enabled(packet));
#endif
      if (hci_event_encryption_change_get_status(packet) == ERROR_CODE_SUCCESS &&
          hci_event_encryption_change_get_encryption_enabled(packet) != 0)
        self->bonds.on_encrypted(
            hci_event_encryption_change_get_connection_handle(packet));
      break;
    case HCI_EVENT_CONNECTION_REQUEST: {
      BdAddr addr;
      hci_event_connection_request_get_bd_addr(packet, addr.data());
      self->bonds.on_connection_request(addr);
      break;
    }
    case HCI_EVENT_LINK_KEY_NOTIFICATION: {
      BdAddr addr;
      // Same layout as the request (BTstack's hci.c reads it this way too)
      hci_event_link_key_request_get_bd_addr(packet, addr.data());
      if (self->bonds.on_link_key(addr))
        self->gap.exit_pairing();
      break;
    }
    case HCI_EVENT_CONNECTION_COMPLETE: {
      // A failure (e.g. page timeout for an absent phone) advances the boot
      // sequence to the next phone.
      uint8_t status = hci_event_connection_complete_get_status(packet);
      BdAddr addr;
      hci_event_connection_complete_get_bd_addr(packet, addr.data());
      if (status == ERROR_CODE_SUCCESS) {
        hci_con_handle_t handle =
            hci_event_connection_complete_get_connection_handle(packet);
        printf("CONNECTED to %s (handle 0x%04x)\n", bd_addr_to_str(addr.data()),
               handle);
        self->bonds.on_connected(handle, addr);
      } else {
        printf("connection to %s failed, status 0x%02x\n",
               bd_addr_to_str(addr.data()), status);
        self->bonds.on_connect_failed(addr);
      }
      break;
    }
    case HCI_EVENT_DISCONNECTION_COMPLETE: {
      uint8_t reason = hci_event_disconnection_complete_get_reason(packet);
      printf("DISCONNECTED, reason 0x%02x\n", reason);
      self->bonds.on_disconnected(
          hci_event_disconnection_complete_get_connection_handle(packet),
          reason);
      break;
    }
    default: break;
  }
}

} // namespace avclan::bluetooth
