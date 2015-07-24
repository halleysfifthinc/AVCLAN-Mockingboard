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

#include "GlobalDef.h"
#include "com232.h"
#include "avclandrv.h"


// -------------------------------------------------------------------------------------
void Setup();

byte rcv_command[5];
byte rcv_pos = 0;
byte rcv_time_clr = 0;

// -------------------------------------------------------------------------------------




// -------------------------------------------------------------------------------------
//	MAIN PROGRAM
//
int main()
{
// byte h;


 byte readSeq = 0;
 byte s_len	= 0;
 byte s_dig	= 0;
 byte s_c[2];
 byte i;
 byte data_tmp[32];

 Setup();



 RS232_S((unsigned char)PSTR("AVCLan reader 1.00\nReady\n\n"));
 LED_OFF();
 RS232_S((unsigned char)PSTR("T - device id\n"));
 RS232_S((unsigned char)PSTR("H - HU id\n"));
 RS232_S((unsigned char)PSTR("S - read sequence\n"));
 RS232_S((unsigned char)PSTR("W - send command\n"));
 RS232_S((unsigned char)PSTR("Q - send broadcast\n"));
 RS232_S((unsigned char)PSTR("L/l - log on/off\n"));
 RS232_S((unsigned char)PSTR("K/k - seq. echo on/off\n"));
 RS232_S((unsigned char)PSTR("R/r - register device\n"));



 while (1) {

	if (INPUT_IS_SET) {	 // if message from some device on AVCLan begin
		LED_ON();
  		AVCLan_Read_Message();
		// show message
	} else {
		LED_OFF();
		// check command from HU
		if (answerReq != 0) AVCLan_SendAnswer();
    }

	// HandleEvent
	switch (Event) {
	  case EV_STATUS:	Event &= ~EV_STATUS;
						AVCLan_Send_Status();
						break;
	}


	// Key handler
	if (RS232_RxCharEnd) {
		cbi(UCSR0B, RXCIE0);								// disable RX complete interrupt
		readkey = RS232_RxCharBuffer[RS232_RxCharBegin];// read begin of received Buffer
		RS232_RxCharBegin++;
		if (RS232_RxCharBegin == RS232_RxCharEnd)		// if Buffer is empty
			RS232_RxCharBegin = RS232_RxCharEnd = 0;	// do reset Buffer
		sbi(UCSR0B, RXCIE0);								// enable RX complete interrupt
		switch (readkey) {
			case 'T':	if (readSeq) {
						  CD_ID_1 = data_tmp[0];
						  CD_ID_2 = data_tmp[1];
						  RS232_S((unsigned char)PSTR("DEV ID SET: 0x"));
						  RS232_PrintHex8(CD_ID_1);
						  RS232_PrintHex8(CD_ID_2);
						  RS232_S((unsigned char)PSTR("\n"));
						  showLog = 1;
						  readSeq=0;
						} else {
						  showLog = 0;
						  RS232_S((unsigned char)PSTR("DEV ID > \n"));
						  readSeq = 1;
						  s_len=0;
						  s_dig=0;
						  s_c[0]=s_c[1]=0;
						}
						break;

			case 'H':	if (readSeq) {
						  HU_ID_1 = data_tmp[0];
						  HU_ID_2 = data_tmp[1];
						  RS232_S((unsigned char)PSTR("HU ID SET: 0x"));
						  RS232_PrintHex8(HU_ID_1);
						  RS232_PrintHex8(HU_ID_2);
						  RS232_S((unsigned char)PSTR("\n"));
						  showLog = 1;
						  readSeq=0;
						} else {
						  showLog = 0;
						  RS232_S((unsigned char)PSTR("HU ID > \n"));
						  readSeq = 1;
						  s_len=0;
						  s_dig=0;
						  s_c[0]=s_c[1]=0;
						}
						break;

			case 'S':	showLog = 0;
						RS232_S((unsigned char)PSTR("READ SEQUENCE > \n"));
						readSeq = 1;
						s_len=0;
						s_dig=0;
						s_c[0]=s_c[1]=0;
						break;
			case 'W' :  showLog = 1;
						readSeq=0;
						AVCLan_SendMyData(data_tmp, s_len);
						break;
			case 'Q' :  showLog = 1;
						readSeq=0;
						AVCLan_SendMyDataBroadcast(data_tmp, s_len);
						break;


			case 'R':	RS232_S((unsigned char)PSTR("REGIST:\n"));
						AVCLan_Command( cmRegister );
          	TCNT0 = 0;
          	while( TCNT0 < 135 );
 						CHECK_AVC_LINE;
						break;
			case 'r':	AVCLan_Register();
						break;


			case 'l':	RS232_S((unsigned char)PSTR("Log OFF\n"));
						showLog = 0;
						break;
			case 'L':	RS232_S((unsigned char)PSTR("Log ON\n"));
						showLog = 1;
						break;

			case 'k':	RS232_S((unsigned char)PSTR("str OFF\n"));
						showLog2 = 0;
						break;
			case 'K':	RS232_S((unsigned char)PSTR("str ON\n"));
						showLog2 = 1;
						break;

			default :
				if (readSeq==1) {
					s_c[s_dig]=readkey;

					s_dig++;
					if (s_dig==2) {
						if (s_c[0]<':') s_c[0] -= 48;
								   else s_c[0] -= 55;
						data_tmp[s_len] = 16 * s_c[0];
						if (s_c[1]<':') s_c[1] -= 48;
								   else s_c[1] -= 55;
						data_tmp[s_len] += s_c[1];
						s_len++;
						s_dig=0;
						s_c[0]=s_c[1]=0;
					}
					if (showLog2) {
						RS232_S((unsigned char)PSTR("CURRENT SEQUENCE > "));
						for (i=0; i<s_len; i++) {
								RS232_PrintHex8(data_tmp[i]);
								RS232_SendByte(' ');
						}
						RS232_S((unsigned char)PSTR("\n"));
					}
				}
		} // switch (readkey)

	}// if (RS232_RxCharEnd)




}
 return 0;
}
// -------------------------------------------------------------------------------------


// -------------------------------------------------------------------------------------
// Setup - uP: ATMega328p
//
void Setup()
{
// GIMSK = 0;			// (GICR ?) disable external interupts

 CD_ID_1 = 0x02;
 CD_ID_2 = 0x80;

 HU_ID_1 = 0x01;
 HU_ID_2 = 0x40;

 showLog = 1;
 showLog2 = 1;

 MCUCR = 0;

 // Timer 1
 TIMSK1 = 0;
 sbi(TIMSK1, OCIE1A); // Enable timer1 compare interrupt
 TCCR1A = 0;
 TCCR1B |= (1 << WGM12)|(1 << CS12); // Set CTC, prescaler at 256
 //TCNT1  = 0xFFFF - 0x7080;
 OCR1A = 62499; // Compare match at 1sec intervals


 RS232_Init();


 AVCLan_Init();

 Event = EV_NOTHING;
 sei();


}
// -------------------------------------------------------------------------------------


byte s1=0;
//------------------------------------------------------------------------------
ISR(TIMER1_COMPA_vect)					// Timer1 compare match every 1Sec
{
	s1++;
	if (s1==2) {
		s1=0;
		if (CD_Mode==stPlay) {
			cd_Time_Sec=HexInc(cd_Time_Sec);
			if (cd_Time_Sec==0x60) {
				cd_Time_Sec = 0;
				cd_Time_Min=HexInc(cd_Time_Min);
				if (cd_Time_Min==0xA0) {
					cd_Time_Min=0x0;
				}
			}
			Event |= EV_STATUS;
		}
	}
}
//------------------------------------------------------------------------------
