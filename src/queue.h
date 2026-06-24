// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Queue_struct {
  uint8_t write;
  uint8_t read;
  uint8_t size; // MUST be a power of 2 (qMask depends on it)
  void **buf;   // size entries, owned by caller
} Queue_t;

void constructQueue(Queue_t *q, void **slots, void *items, uint8_t itemSize,
                    uint8_t len, bool constructFull);
void constructEmptyQueue(Queue_t *q, void **slots, uint8_t len);
bool isEmpty(const Queue_t *q);
static inline void incrementRead(Queue_t *q) { q->read++; }
uint8_t pushQueue(Queue_t *q, void *x);
void *peekQueue(const Queue_t *q);
void *popQueue(Queue_t *q);

#ifdef __cplusplus
}
#endif
