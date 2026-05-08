#pragma once

#include <stdint.h>

typedef struct Queue_struct {
  uint8_t write;
  uint8_t read;
  uint8_t size;
  void *buf[];
} Queue_t;

void constructQueue(Queue_t *q, void *buf, uint8_t size, uint8_t len,
                    uint8_t constructFull);
void constructEmptyQueue(Queue_t *q, uint8_t size, uint8_t len);
uint8_t isEmpty(const Queue_t *q);
uint8_t pushQueue(Queue_t *q, void *x);
const void *peekQueue(const Queue_t *q);
void *popQueue(Queue_t *q);
