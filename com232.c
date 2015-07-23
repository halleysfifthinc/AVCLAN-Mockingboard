/*
  Copyright (C) 2006 Marcin Slonicki <marcin@softservice.com.pl>.

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software Foundation,
  Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

 -----------------------------------------------------------------------
	this file is a part of the TOYOTA Corolla MP3 Player Project
 -----------------------------------------------------------------------
 		http://www.softservice.com.pl/corolla/avc

 May 28 / 2009	- version 2

*/

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include "com232.h"


//------------------------------------------------------------------------------
void RS232_Init(void)
{
 // init LED
 sbi(DDRB, 5);
 cbi(PORTB, 5);

 RS232_RxCharBegin = RS232_RxCharEnd = 0;

 UCSR0A = 0;
 UCSR0B = ((1<<RXCIE0) | (1<<RXEN0) | (1<<TXEN0));	// enable RxD/TxD and interrupts
 UCSR0C = ((1<<UCSZ01)|(1<<UCSZ00));				// 8N1
 UBRR0L  = 3; // 250000

}
//------------------------------------------------------------------------------
SIGNAL(SIG_USART_RECV)
{
	RS232_RxCharBuffer[RS232_RxCharEnd] = UDR0;		// Store received character to the End of Buffer
    RS232_RxCharEnd++;
}
//------------------------------------------------------------------------------
void RS232_SendByte(u08 Data)
{
	while ((UCSR0A & _BV(UDRE0)) != _BV(UDRE0));	// wait for UART to become available
	UDR0 = Data;									// send character
}
//------------------------------------------------------------------------------
void RS232_S(u16 str_addr)
{
	register u08 c;
	while ( (c = pgm_read_byte(str_addr++) ) )
	{
		if (c == '\n')
			RS232_SendByte('\r');
		RS232_SendByte(c);
	}
}
//------------------------------------------------------------------------------
void RS232_Print(char* pBuf)
{
	register u08 c;
	while ((c = *pBuf++))
	{
		if (c == '\n')
			RS232_SendByte('\r');
		RS232_SendByte(c);
	}
}
//------------------------------------------------------------------------------
void RS232_PrintHex4(u08 Data)
{
	u08 Character = Data & 0x0f;
	Character += '0';
	if (Character > '9')
		Character += 'A' - '0' - 10;
	RS232_SendByte(Character);
}
//------------------------------------------------------------------------------
void RS232_PrintHex8(u08 Data)
{
    RS232_PrintHex4(Data >> 4);
    RS232_PrintHex4(Data);
}
//------------------------------------------------------------------------------
void RS232_PrintDec(u08 Data)
{
 if (Data>99) {
   RS232_SendByte('*');
   return;
 }
 if (Data<10) {
   RS232_SendByte('0'+Data);
   return;
 }
 u08 c;
 u16 v,v1;
 v  = Data;
 v1 = v/10;
 c  = '0' + (v-v1*10);
 RS232_SendByte('0'+v1);
 RS232_SendByte(c);
}
//------------------------------------------------------------------------------
void RS232_PrintDec2(u08 Data)
{
 if (Data<10) RS232_SendByte('0');
 RS232_PrintDec(Data);
}
//------------------------------------------------------------------------------
