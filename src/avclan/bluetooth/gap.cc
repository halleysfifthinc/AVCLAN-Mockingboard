// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "btstack.h" // IWYU pragma: export

#include "gap.hpp"
#include "sdp.hpp"

namespace avclan::bluetooth {

void Gap::setup() {
  // BTstack fills in the addr
  gap_set_local_name("Mockingboard 00:00:00:00:00:00");

  // Class of Device Ref:
  // https://bitbucket.org/bluetooth-SIG/public/src/main/assigned_numbers/core/class_of_device.yaml
  //                         ╭──── Major service class: Audio
  //                         │╭─── Major service class: Rendering
  //                         ││ ╭─ Major device class:  Audio/Video
  //                         ││ │╭ Minor device class:  Car audio
  //                         ││ ││
  constexpr uint32_t COD = 0x240420;
  gap_set_class_of_device(COD);

  // Extended Inquiry Response (EIR): the name and services, before a connection
  static constexpr auto eir = [] {
    constexpr std::string_view name = "Mockingboard";
    constexpr std::array<uint16_t, 3> services = {
        BLUETOOTH_SERVICE_CLASS_AUDIO_SINK,
        BLUETOOTH_SERVICE_CLASS_AV_REMOTE_CONTROL_TARGET,
        BLUETOOTH_SERVICE_CLASS_AV_REMOTE_CONTROL,
    };
    // HCI_EXTENDED_INQUIRY_RESPONSE_DATA_LEN = 240 (gap.h doc comment)
    std::array<uint8_t, 240> data{};
    size_t i = 0;
    data[i++] = static_cast<uint8_t>(1 + name.size());
    data[i++] = BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME;
    for (char c : name)
      data[i++] = static_cast<uint8_t>(c);
    data[i++] = static_cast<uint8_t>(1 + (sizeof(uint16_t) * services.size()));
    data[i++] = BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS;
    for (uint16_t uuid : services) { // little-endian
      data[i++] = static_cast<uint8_t>(uuid);
      data[i++] = static_cast<uint8_t>(uuid >> 8);
    }
    return data;
  }();
  gap_set_extended_inquiry_response(eir.data());

  gap_discoverable_control(0);
  gap_connectable_control(1);

  // Select the "Numeric Comparison" model of Secure Simple Pairing (SSP)
  // Numeric comparison shows a 6-digit code on both devices and requires user
  // confirmation that the codes match
  gap_ssp_set_io_capability(SSP_IO_CAPABILITY_DISPLAY_YES_NO);

  gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_ROLE_SWITCH |
                                       LM_LINK_POLICY_ENABLE_SNIFF_MODE);
  gap_set_allow_role_switch(true);

  // Device ID (PnP) record: expected but basically non-functional.
  // Uses BTstack's own BT SIG company ID for lack of a better choice.
  device_id_create_sdp_record(device_id_record,
                              sdp_create_service_record_handle(),
                              DEVICE_ID_VENDOR_ID_SOURCE_BLUETOOTH,
                              BLUETOOTH_COMPANY_ID_BLUEKITCHEN_GMBH, 1, 1);
  register_sdp_record(device_id_record, "Device ID");
}

void Gap::enter_pairing() {
  static constexpr uint32_t PAIRING_WINDOW_MS = 2 * 60 * 1000;

  auto pairing_timeout_handler = [](btstack_timer_source_t *ts) static {
    static_cast<Gap *>(btstack_run_loop_get_timer_context(ts))->exit_pairing();
  };

  pairing_open = true;
  gap_connectable_control(1);
  // 640 ms interval (default 2.56 s), 11.25 ms window
  gap_inquiry_set_scan_activity(0x0400, 0x0012);
  gap_discoverable_control(1);
  // Pairing is only accepted while the window is open
  gap_ssp_set_auto_accept(1);

  btstack_run_loop_remove_timer(&pairing_timer);
  btstack_run_loop_set_timer_handler(&pairing_timer, pairing_timeout_handler);
  btstack_run_loop_set_timer_context(&pairing_timer, this);
  btstack_run_loop_set_timer(&pairing_timer, PAIRING_WINDOW_MS);
  btstack_run_loop_add_timer(&pairing_timer);
  printf("pairing window open for %" PRIu32 " s\n", PAIRING_WINDOW_MS / 1000);
}

void Gap::exit_pairing() {
  if (!pairing_open)
    return;
  pairing_open = false;
  btstack_run_loop_remove_timer(&pairing_timer);
  gap_discoverable_control(0);
  gap_ssp_set_auto_accept(0);
  puts("pairing window closed; now connectable-only (not discoverable)");
}

} // namespace avclan::bluetooth
