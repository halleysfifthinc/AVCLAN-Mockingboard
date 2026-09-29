// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdio>

#include "FreeRTOS.h" // IWYU pragma: export
#include "task.h"

#include "pico/cyw43_arch.h"
#include <pico/async_context_freertos.h>

#include "avclan.h"
#include "bluetooth/platform.h"

namespace {
constexpr UBaseType_t BT_CORE = 1;
} // namespace

namespace avclan::bluetooth::platform {

void init() {
  static async_context_freertos_t context;
  async_context_freertos_config_t cfg = async_context_freertos_default_config();
  cfg.task_priority = BToothPriority;
  cfg.task_core_id = BT_CORE;
  if (!async_context_freertos_init(&context, &cfg)) {
    puts("BT async context init failed");
    return;
  }
  cyw43_arch_set_async_context(&context.core);

  // `cyw43_arch_init` and other BT configurations assume an active scheduler
  // Create/schedule a one-shot task to be run once the scheduler is started
  auto init_bt = [](void *) static {
    if (cyw43_arch_init() != 0) {
      puts("CYW43 init failed");
      return;
    }
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);

    vTaskDelete(nullptr);
  };
  // The CYW43 firmware download blocks this task; keep it off the bus core
  xTaskCreateAffinitySet(init_bt, "init BT", 1000, nullptr, BToothPriority,
                         1U << BT_CORE, nullptr);
}

} // namespace avclan::bluetooth::platform
