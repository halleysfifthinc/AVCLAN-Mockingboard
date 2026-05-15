#include <stdint.h>

#include "avclandrv.h"
#include "queue.h"

void constructQueue(Queue_t *q, void **slots, void *items, uint8_t itemSize,
                    uint8_t len, uint8_t constructFull) {
  q->read = 0;
  q->size = len;
  q->buf = slots;
  if (!!constructFull) {
    q->write = len;

    for (uint8_t i = 0; i < len; ++i) {
      q->buf[i] = items;
      items = (char *)items + itemSize;
    }
  } else {
    q->write = 0;
  }
}

void constructEmptyQueue(Queue_t *q, void **slots, uint8_t len) {
  constructQueue(q, slots, nullptr, 0, len, 0);
}

uint8_t isEmpty(const Queue_t *q) { return (q->write == q->read); }

static inline uint8_t isFull(const Queue_t *q) {
  return ((q->write - q->read) == q->size);
}

static inline uint8_t qMask(const Queue_t *q, uint8_t pos) {
  return pos & (q->size - 1);
}

uint8_t pushQueue(Queue_t *q, void *x) {
  if (x == nullptr || isFull(q))
    return 1;

  q->buf[qMask(q, q->write++)] = x;

  return 0;
}

void *peekQueue(const Queue_t *q) {
  if (isEmpty(q))
    return nullptr;

  return q->buf[qMask(q, q->read)];
}

void *popQueue(Queue_t *q) {
  if (isEmpty(q))
    return nullptr;

  return q->buf[qMask(q, q->read++)];
}
