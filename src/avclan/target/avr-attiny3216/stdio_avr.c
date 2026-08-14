// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <avr/io.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <util/atomic.h>

#include "hal/stdio.h"
#include "timing_avr.h" // IWYU pragma: export
#include "usart.h"      // jnk0le AVR-UART-lib

// TX ring owned by the jnk0le lib. `tx0_Head` is the index of the last byte
// written, so the next write goes to head+1. The DRE ISR reads tail+1 and then
// advances `tx0_Tail`. The ring is empty when the two indexes are equal. The
// ring holds at most TX0_BUFFER_SIZE-1 bytes.
extern char tx0_buffer[TX0_BUFFER_SIZE];

// stdio_write_nonblock() is all-or-nothing. If the ring cannot hold the largest
// buffer that the app writes, the function drops every buffer and queues none.
// The largest buffer is a text frame log line (Frame::print). That line is
// ~186 bytes at MAXLENGTH=32. The ring therefore needs headroom above this
// size.
static_assert(TX0_BUFFER_SIZE - 1 >= 192,
              "TX ring too small to hold a whole frame log line");

// Raw stdout backend: emit the byte verbatim, no '\n' -> "\r\n" translation
// (unlike the lib's uart_putchar). Keeps binary frame payloads intact.
static int stdio_putchar(char data, FILE *stream) {
  (void)stream;
  uart0_putc(data);
  return 0;
}

bool stdio_write_nonblock(const void *buf, size_t len) {
  if (len == 0)
    return true;

  // A single unguarded read of each index is enough:
  //  - Only main writes tx0_Head.
  //  - Only the DRE ISR writes tx0_Tail.
  //  - 8-bit accesses are atomic on AVR.
  // If the ISR advances the tail during this function, the ring has more free
  // space than this function measured. That direction is safe.
  const uint8_t head = tx0_Head;
  const uint8_t space = (uint8_t)((tx0_Tail - head - 1) & TX0_BUFFER_MASK);
  if (len > space) {
    // The ring cannot take the whole buffer, so drop it and queue the indicator
    // instead. A buffer may be refused because it is larger than
    // available space or because the ring is full. For the former case, the
    // full indicator can be appended without truncating previously queued
    // bytes.
    if (space >= 3) {
      tx0_buffer[(head + 1) & TX0_BUFFER_MASK] = '\n';
      tx0_buffer[(head + 2) & TX0_BUFFER_MASK] = '!';
      tx0_buffer[(head + 3) & TX0_BUFFER_MASK] = '\n';
      tx0_Head = (uint8_t)((head + 3) & TX0_BUFFER_MASK);
      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { USART0.CTRLA |= USART_DREIE_bm; }
    } else {
      // No room for even the indicator, so it goes over the last three queued
      // bytes. `space` and the queued byte count always sum to
      // TX0_BUFFER_MASK, so space < 3 means at least 253 bytes are queued and
      // all three writes land inside them. The ISR already drains the ring, so
      // DREIE needs no change.
      tx0_buffer[(head - 2) & TX0_BUFFER_MASK] = '\n';
      tx0_buffer[(head - 1) & TX0_BUFFER_MASK] = '!';
      tx0_buffer[head] = '\n';
    }
    return false;
  }

  const uint8_t start = (uint8_t)((head + 1) & TX0_BUFFER_MASK);
  const size_t contiguous = TX0_BUFFER_SIZE - start;

  if (len <= contiguous) {
    memcpy(&tx0_buffer[start], buf, len);
  } else { // wraps the end of the ring
    memcpy(&tx0_buffer[start], buf, contiguous);
    memcpy(&tx0_buffer[0], (const char *)buf + contiguous, len - contiguous);
  }

  // Publish the bytes only after all of them are in place. This is a single
  // store, so the ISR never sees a partly filled buffer.
  tx0_Head = (uint8_t)((head + len) & TX0_BUFFER_MASK);

  // The ISR also writes CTRLA (to clear DREIE when it drains the last byte).
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { USART0.CTRLA |= USART_DREIE_bm; }
  return true;
}

// Non-blocking stdin backend: next received byte, or _FDEV_EOF when the RX ring
// is empty (uart0_getData() returns a negative value when there is no data).
static int stdio_getchar(FILE *stream) {
  (void)stream;
  int16_t c = uart0_getData();
  return c < 0 ? _FDEV_EOF : c;
}

// One raw RW stream for the stdio UART: verbatim byte output, non-blocking
// input.
static FILE stdio_stream =
    FDEV_SETUP_STREAM(stdio_putchar, stdio_getchar, _FDEV_SETUP_RW);

void stdio_init(void) {
  // The lib's uart0_init configures the USART registers/baud but not the pins;
  // keep the ATtiny3216 pin-mux the old driver did.
  PORTMUX.CTRLB = PORTMUX_USART0_ALTERNATE_gc; // TxD/RxD on PA1/PA2
  PORTA.DIRSET = PIN1_bm;                      // TxD output
  PORTA.DIRCLR = PIN2_bm;                      // RxD input

#ifdef USE_DOUBLE_SPEED
  uart0_init(DOUBLE_BAUD_CALC(1200000));
#else
  uart0_init(BAUD_CALC(1200000));
#endif

  stdout = stdin = &stdio_stream; // printf/fputs/fwrite + non-blocking getchar
}
