// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>

#include "FreeRTOS.h" // IWYU pragma: export
#include "queue.h"

// Owning FIFO of `T`s, safe to share between tasks
template <class T> class Queue {
public:
  explicit Queue(UBaseType_t length)
      : handle{xQueueCreate(length, sizeof(T *))} {
    configASSERT(handle);
  }
  Queue(const Queue &) = delete;

  // Blocks until accepted (INCLUDE_vTaskSuspend: portMAX_DELAY never times out)
  void push(std::unique_ptr<T> x) {
    T *ptr = x.release();
    xQueueSend(handle, &ptr, portMAX_DELAY);
  }

  std::unique_ptr<T> pop(TickType_t wait = portMAX_DELAY) {
    T *ptr = nullptr;
    xQueueReceive(handle, &ptr, wait);
    return std::unique_ptr<T>(ptr);
  }

  // The front item stays owned by the queue
  const T *peek(TickType_t wait = portMAX_DELAY) const {
    T *ptr = nullptr;
    xQueuePeek(handle, &ptr, wait);
    return ptr;
  }

private:
  QueueHandle_t handle;
};
