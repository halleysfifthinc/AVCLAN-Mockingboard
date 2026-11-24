// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdio>

#include "FreeRTOS.h" // IWYU pragma: export
#include "task.h"

#include "pico/cyw43_arch.h"
#include <pico/async_context.h>
#include <pico/async_context_freertos.h>

#include "avclan.h"
#include "bluetooth/platform.h"

namespace {
constexpr UBaseType_t BT_CORE = 1;

async_when_pending_worker_t worker;
} // namespace

namespace avclan::bluetooth::platform {

void init(void (*setup)(void *), void (*work)(void *), void *ctx) {
  static auto setup_fn = setup;
  static auto work_fn = work;
  static void *work_ctx = ctx;
  worker.do_work = [](async_context_t *, async_when_pending_worker_t *) static {
    work_fn(work_ctx);
  };

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

    async_context_t *ctx = cyw43_arch_async_context();
    async_context_acquire_lock_blocking(ctx);
    setup_fn(work_ctx);
    async_context_add_when_pending_worker(ctx, &worker);
    async_context_release_lock(ctx);

    // Last measured at 850 words
    // printf("stack highwatermark: %lu", uxTaskGetStackHighWaterMark(nullptr));

    vTaskDelete(nullptr);
  };
  // The CYW43 firmware download blocks this task; keep it off the bus core
  xTaskCreateAffinitySet(init_bt, "init BT", 1000, nullptr, BToothPriority,
                         1U << BT_CORE, nullptr);
}

void wake() {
  async_context_set_work_pending(cyw43_arch_async_context(), &worker);
}

} // namespace avclan::bluetooth::platform
