// copyright (C) 2006 Marcin Slonicki <marcin@softservice.com.pl>
// copyright (C) 2007 Louis Frigon
// Copyright (C) 2015 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef __COM232_H
#define __COM232_H

#include <stdint.h>

void RS232_Init(void);

// Receive path. The RX ring is private to the driver; the app polls hasChar()
// and drains with getChar(). setRxInterrupt() masks/unmasks RX completion (used
// by the bus-transaction guard).
void RS232_setRxInterrupt(bool enable);
bool RS232_hasChar(void);
char RS232_getChar(void);

void RS232_Print_P(const char *str_addr);
void RS232_SendByte(uint8_t Data);
void RS232_sendbytes(const uint8_t *bytes, uint8_t len);
void RS232_Print(const char *pBuf);
void RS232_PrintHex4(uint8_t Data);
void RS232_PrintHex8(uint8_t Data);
void RS232_PrintHex12(uint16_t x);
void RS232_PrintHex(uint16_t x);
void RS232_PrintDec(uint8_t Data);
void RS232_PrintDec2(uint8_t Data);

#endif // __COM232_H
