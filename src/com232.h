/*
                        AVCLAN-Mockingboard
    Copyright (C) 2015 Allen Hill <allenofthehills@gmail.com>

    Portions of the following source code are based on code that is
    copyright (C) 2006 Marcin Slonicki <marcin@softservice.com.pl>

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

extern uint8_t RS232_RxCharBuffer[25], RS232_RxCharBegin, RS232_RxCharEnd;
extern uint8_t readkey;

void RS232_Init(void);
extern void RS232_Print_P(const char *str_addr);
extern void RS232_SendByte(uint8_t Data);
extern void RS232_Print(const char *pBuf);
extern void RS232_PrintHex4(uint8_t Data);
extern void RS232_PrintHex8(uint8_t Data);
extern void RS232_PrintDec(uint8_t Data);
extern void RS232_PrintDec2(uint8_t Data);
extern char *itoa(int i, char b[]);

#endif // __COM232_H
