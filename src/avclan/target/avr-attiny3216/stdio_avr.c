// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <avr/io.h>
#include <stdint.h>
#include <stdio.h>

#include "hal/stdio.h"
#include "timing_avr.h" // IWYU pragma: export
#include "usart.h"      // jnk0le AVR-UART-lib

// Raw stdout backend: emit the byte verbatim, no '\n' -> "\r\n" translation
// (unlike the lib's uart_putchar). Keeps binary frame payloads intact.
static int stdio_putchar(char data, FILE *stream) {
  (void)stream;
  uart0_putc(data);
  return 0;
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
  PORTA.DIRSET = PIN1_bm;                       // TxD output
  PORTA.DIRCLR = PIN2_bm;                       // RxD input

  #ifdef USE_DOUBLE_SPEED
  uart0_init(DOUBLE_BAUD_CALC(1200000));
  #else
  uart0_init(BAUD_CALC(1200000));
  #endif

  stdout = stdin = &stdio_stream; // printf/fputs/fwrite + non-blocking getchar
}
