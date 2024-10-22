/*
                        AVCLAN-Mockingboard
    Copyright (C) 2015 Allen Hill <allenofthehills@gmail.com>

    Portions of the following source code are based on code that is
    copyright (C) 2006 Marcin Slonicki <marcin@softservice.com.pl>
    copyright (C) 2007 Louis Frigon

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/sfr_defs.h>
#include <stdint.h>

#include "com232.h"
#include "timing.h"

#if USART_RXMODE == USART_RXMODE_CLK2X_gc
  #define RXMODE_S 8
#elif USART_RXMODE == USART_RXMODE_NORMAL_gc
  #define RXMODE_S 16
#endif

#define USART_BAUD_RATE(BAUD_RATE)                                             \
  (uint16_t)((float)(F_CPU * 64 / (RXMODE_S * (float)BAUD_RATE)) + 0.5)

uint8_t RS232_RxCharBuffer[25], RS232_RxCharBegin, RS232_RxCharEnd;

void RS232_Init(void) {
  RS232_RxCharBegin = RS232_RxCharEnd = 0;

  PORTMUX.CTRLB = PORTMUX_USART0_ALTERNATE_gc; // Use PA1/PA2 for TxD/RxD

  PORTA.DIRSET = PIN1_bm;
  PORTA.DIRCLR = PIN2_bm;

  USART0.CTRLA = USART_RXCIE_bm;                 // Enable receive interrupts
  USART0.CTRLB = USART_RXEN_bm | USART_TXEN_bm | // Enable Rx/Tx and set receive
                 USART_RXMODE;                   // mode
  USART0.CTRLC = USART_CMODE_ASYNCHRONOUS_gc | USART_PMODE_DISABLED_gc |
                 USART_CHSIZE_8BIT_gc |
                 USART_SBMODE_1BIT_gc; // Async UART with 8N1 config
  USART0.BAUD = USART_BAUD_RATE(1200000);
}

ISR(USART0_RXC_vect) {
  // Store received character to the End of Buffer
  RS232_RxCharBuffer[RS232_RxCharEnd] = USART0_RXDATAL;
  RS232_RxCharEnd++;
}

void RS232_SendByte(uint8_t Data) {
  loop_until_bit_is_set(USART0_STATUS,
                        USART_DREIF_bp); // wait for UART to become available
  USART0_TXDATAL = Data;                 // send character
}

void RS232_sendbytes(const uint8_t *bytes, uint8_t len) {
  for (uint8_t i = 0; i < len; i++) {
    RS232_SendByte(bytes[i]);
  }
}

void RS232_Print(const char *pBuf) {
  register uint8_t c;
  while ((c = *pBuf++)) {
    if (c == '\n')
      RS232_SendByte('\r');
    RS232_SendByte(c);
  }
}

void RS232_PrintHex4(uint8_t Data) {
  uint8_t Character = Data & 0x0f;
  Character += '0';
  if (Character > '9')
    Character += 'A' - '0' - 10;
  RS232_SendByte(Character);
}

void RS232_PrintHex8(uint8_t Data) {
  RS232_PrintHex4(Data >> 4);
  RS232_PrintHex4(Data);
}

void RS232_PrintHex12(uint16_t x) {
  RS232_PrintHex4(*(((uint8_t *)&x) + 1));
  RS232_PrintHex8(*(((uint8_t *)&x) + 0));
}

void RS232_PrintDec(uint8_t Data) {
  if (Data > 99) {
    RS232_SendByte('*');
    return;
  }
  if (Data < 10) {
    RS232_SendByte('0' + Data);
    return;
  }
  uint8_t c;
  unsigned short v, v1;
  v = Data;
  v1 = v / 10;
  c = '0' + (v - v1 * 10);
  RS232_SendByte('0' + v1);
  RS232_SendByte(c);
}

void RS232_PrintDec2(uint8_t Data) {
  if (Data < 10)
    RS232_SendByte('0');
  RS232_PrintDec(Data);
}
