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
