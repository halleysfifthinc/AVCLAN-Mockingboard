/*
  Copyright (C) 2015 Allen Hill <allenofthehills@gmail.com>.

  Portions of the following source code are:
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
//#include <avr/pgmspace.h> // Unnecessary?
#include "avclandrv.h"
#include "com232.h"
#include "GlobalDef.h"


//------------------------------------------------------------------------------

#define AVC_OUT_EN()	sbi(PORTD, 6); sbi(DDRD, 6);  sbi(DDRD, 7); sbi(ACSR, ACD);
#define AVC_OUT_DIS()	cbi(PORTD, 6); cbi(DDRD, 6);  cbi(DDRD, 7); cbi(ACSR, ACD);
#define AVC_SET_1()  	sbi(PORTD, 6);
#define AVC_SET_0()  	cbi(PORTD, 6);


byte CD_ID_1;
byte CD_ID_2;

byte HU_ID_1;
byte HU_ID_2;

byte parity_bit;

byte repeatMode;
byte randomMode;

byte playMode;

byte cd_Disc;
byte cd_Track;
byte cd_Time_Min;
byte cd_Time_Sec;

byte answerReq;

// we need check answer (to avclan check) timeout
// when is more then 1 min, FORCE answer.
byte check_timeout;

#define SW_ID	0x11 // 11 For my stereo

// commands
const byte stat1[]		= { 0x4, 0x00, 0x00, 0x01, 0x0A };
const byte stat2[]		= { 0x4, 0x00, 0x00, 0x01, 0x08 };
const byte stat3[]		= { 0x4, 0x00, 0x00, 0x01, 0x0D };
const byte stat4[] 		= { 0x4, 0x00, 0x00, 0x01, 0x0C };

// Updated avclandrv.c had these
const u08 stat5[]			= { 0x6, 0x00, 0x12, 0x5C, 0x43, 0x02, 0x00 };
const u08 stat6[]			= { 0x4, 0x00, 0x58, 0x63, 0xE0 };

// broadcast
const byte lan_stat1[]	= { 0x3, 0x00, 0x01, 0x0A };
const byte lan_reg[]		= { 0x3, SW_ID, 0x01, 0x00 };
const byte lan_init[]		= { 0x3, SW_ID, 0x01, 0x01 };
const byte lan_check[]	= { 0x3, SW_ID, 0x01, 0x20 };
const byte lan_playit[]	= { 0x4, SW_ID, 0x01, 0x45, 0x63 };



const byte play_req1[]	= { 0x4,	0x00, 0x25, 0x63, 0x80 };

#ifdef __AVENSIS__
	const byte play_req2[]	= { 0x6,	0x00, SW_ID, 0x63, 0x42 };
#else
	const byte play_req2[]	= { 0x6,	0x00, SW_ID, 0x63, 0x42, 0x01, 0x00 };
#endif

const byte play_req3[]	= { 0x6,	0x00, SW_ID, 0x63, 0x42, 0x41, 0x00 };
const byte stop_req[]	= { 0x5,	0x00, SW_ID, 0x63, 0x43, 0x01 };
const byte stop_req2[]	= { 0x5,	0x00, SW_ID, 0x63, 0x43, 0x41 };


// answers
const byte	CMD_REGISTER[]	= {0x1,		0x05,	0x00, 0x01,	SW_ID, 0x10, 0x63 };
const byte	CMD_STATUS1[]	= {0x1,		0x04,	0x00, 0x01, 0x00, 0x1A };
const byte	CMD_STATUS2[]	= {0x1,		0x04,	0x00, 0x01, 0x00, 0x18 };
const byte	CMD_STATUS3[]	= {0x1,		0x04,	0x00, 0x01, 0x00, 0x1D };
const byte	CMD_STATUS4[]	= {0x1,		0x05,	0x00, 0x01, 0x00, 0x1C, 0x00 };
byte			CMD_CHECK[]		= {0x1,		0x06,	0x00, 0x01, SW_ID, 0x30, 0x00, 0x00 };

const byte	CMD_STATUS5[]	= {0x1,		0x05,	0x00, 0x5C, 0x12, 0x53, 0x02 };
const byte	CMD_STATUS5A[]	= {0x0,		0x05,	0x5C, 0x31, 0xF1, 0x00, 0x00 };

const byte	CMD_STATUS6[]	= {0x1,		0x06,	0x00, 0x5C, 0x32, 0xF0, 0x02, 0x00 };


const byte	CMD_PLAY_OK1[]	= {0x1,		0x05,	0x00, 0x63, SW_ID, 0x50, 0x01 };
const byte	CMD_PLAY_OK2[]	= {0x1,		0x05,	0x00, 0x63, SW_ID, 0x52, 0x01 };
const byte	CMD_PLAY_OK3[]	= {0x0,		0x0B,	0x63, 0x31, 0xF1, 0x01, 0x00, 0x01, 0xFF, 0xFF, 0xFF, 0x00, 0x80 };
byte			CMD_PLAY_OK4[]	= {0x0,		0x0B,	0x63, 0x31, 0xF1, 0x01, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80 };

const byte	CMD_STOP1[]		= {0x1,		0x05,	0x00, 0x63, SW_ID, 0x53, 0x01 };
byte			CMD_STOP2[]		= {0x0,		0x0B,	0x63, 0x31, 0xF1, 0x00, 0x30, 0x00, 0x00,0x00, 0x00, 0x00, 0x80 };

const byte	CMD_BEEP[]		= {0x1,		0x05,	0x00, 0x63, 0x29, 0x60, 0x02 };

//------------------------------------------------------------------------------


// DONE: Timing adjusted, however refactoring may make code more clear/efficient
//------------------------------------------------------------------------------
void AVC_HoldLine()
{
 STOPEvent;

 // wait for free line
 byte line_busy = 1;

 TCNT0 = 0;
 do {
 	while (INPUT_IS_CLEAR) {
		/*	The comparison value was originally 25 with CK64 (tick period of 4.34 us)
		 		at a clock frequency 14.7456MHz.
				For a more accurate tick period of .5 us at 16MHz, the value should be approximately 225*/
		if (TCNT0 >= 225) break;
 	}
 	if (TCNT0 > 216) line_busy=0;
 } while (line_busy);

 // switch to out mode
 AVC_OUT_EN();
 AVC_SET_1();

 STARTEvent;
}

// DONE: No changes necessary
//------------------------------------------------------------------------------
void AVC_ReleaseLine()
{
 AVC_SET_0();
 AVC_OUT_DIS();
}
//------------------------------------------------------------------------------


// DONE: No changes necessary
//------------------------------------------------------------------------------
void AVCLan_Init()
{
	// OUTPUT ( set as input for comparator )
	cbi(PORTD, 6);
	cbi(DDRD, 6);

	// INPUT
	cbi(PORTD, 7);
	cbi(DDRD, 7);

	// Analog comparator

	cbi(ADCSRB, ACME);	// Analog Comparator Multiplexer Enable - NO
	/*
	cbi(ACSR, ACBG);	// Analog Comparator Bandgap Select
						// ACI: Analog Comparator Interrupt Flag

	cbi(ACSR, ACIE);	// Analog Comparator Interrupt Enable - NO
	cbi(ACSR, ACIC);	// Analog Comparator Input Capture Enable - NO
	*/
	cbi(ACSR, ACIS1);	// Analog Comparator Interrupt Mode Select
	cbi(ACSR, ACIS0);  // Comparator Interrupt on Output Toggle

	cbi(ACSR, ACD); 	// Analog Comparator Disable - NO

	TCCR0B = _BV(CS01);	// Timer 0 prescaler = 8 ( 2 count / us )

	message_len   = 0;
	answerReq     = cmNull;
	check_timeout = 0;

	cd_Disc = 1;
	cd_Track = 1;
	cd_Time_Min = 0;
	cd_Time_Sec = 0;
	repeatMode = 0;
	randomMode = 0;
	playMode   = 0;
	CD_Mode    = stStop;

}

// DONE: Timing adjusted, however refactoring may make code more clear/efficient
//------------------------------------------------------------------------------
byte AVCLan_Read_Byte(byte length)
{
 byte bite = 0;

 while (1) {
   while (INPUT_IS_CLEAR);
   TCNT0 = 0;
   while (INPUT_IS_SET);
   if ( TCNT0 < 72 ) {
      bite++;
	  parity_bit++;
   }
   length--;
   if (!length) return bite;
   bite = bite << 1;
 }
}

// DONE: Timing adjusted
//------------------------------------------------------------------------------
byte AVCLan_Send_StartBit()
{
	AVC_SET_1();
	TCNT0 = 0;
	while( TCNT0 < 333 );

	AVC_SET_0();
	TCNT0 = 0;
	while( TCNT0 < 61 );

	return 1;
}

// DONE: Timing adjusted. Comparison values are timer ticks, not us.
//------------------------------------------------------------------------------
void AVCLan_Send_Bit1()
{
	AVC_SET_1();
	TCNT0 = 0;
	while( TCNT0 < 41 );

	AVC_SET_0();
	TCNT0 = 0;
	while( TCNT0 < 33 );							// 12-21 us
}

// DONE: Timing adjusted
//------------------------------------------------------------------------------
void AVCLan_Send_Bit0()
{
	AVC_SET_1();
	TCNT0 = 0;
	while( TCNT0 < 65 );						// 28-37 us

	AVC_SET_0();
	TCNT0 = 0;
	while( TCNT0 < 9 );								// 00-09 us
}

//	DONE: Timing adjusted.
//------------------------------------------------------------------------------
byte AVCLan_Read_ACK()
{
	byte time = 0;

	AVC_SET_1();
	TCNT0 = 0;
	while( TCNT0 < 39 );

	AVC_SET_0();
	TCNT0 = 0;
	while( TCNT0 < 3 );


	AVC_OUT_DIS(); // switch to read mode
	TCNT0 = 1;
	while(1) {
	time = TCNT0;
	if (INPUT_IS_SET && (TCNT0 > 1)) break;
	if (TCNT0 > 41) return 1;					// Not sure if this fix is intent correct
	}

	while(INPUT_IS_SET);
	AVC_OUT_EN();	// back to write mode
	return 0;

}

//	DONE: Timing adjusted.
//------------------------------------------------------------------------------
byte AVCLan_Send_ACK()
{
	TCNT0 = 0;
	while (INPUT_IS_CLEAR)	{
		if (TCNT0 >= 225) return 0;			// max wait time
	}

	AVC_OUT_EN();

	AVC_SET_1();
	TCNT0 = 0;
	while( TCNT0 < 65 );								//28-37

	AVC_SET_0();
	TCNT0 = 0;
	while( TCNT0 < 9 );									//00-09

	AVC_OUT_DIS();

	return 1;
}

// DONE: var 'byte' adjusted to 'bite' to avoid reserved word conflict
//------------------------------------------------------------------------------
byte AVCLan_Send_Byte(byte bite, byte len)
{
 byte b;
 if (len==8) {
 	b = bite;
 } else {
    b = bite << (8-len);
 }

 while (1) {
   if ( (b & 128)!=0 ) {
     AVCLan_Send_Bit1();
	 parity_bit++;
   } else {
   	 AVCLan_Send_Bit0();
   }
   len--;
   if (!len) {
     //if (INPUT_IS_SET) RS232_Print("SBER\n"); // Send Bit ERror
     return 1;
   }
   b = b << 1;
 }

}

// DONE: Nothing needed.
//------------------------------------------------------------------------------
byte AVCLan_Send_ParityBit()
{
 if ( (parity_bit & 1)!=0 ) {
     AVCLan_Send_Bit1();
	 //parity_bit++;
 } else {
   	 AVCLan_Send_Bit0();
 }
 parity_bit=0;
 return 1;
}

// DONE: Nothing needed.
//------------------------------------------------------------------------------
byte CheckCmd(byte *cmd)
{
 byte i;
 byte *c;
 byte l;

 c = cmd;
 l = *c++;

 for (i=0; i<l; i++) {
 	if (message[i] != *c) return 0;
	c++;
 }
 return 1;
}

// DONE: Timing adjusted.
//------------------------------------------------------------------------------
byte AVCLan_Read_Message()
{
 STOPEvent;						// disable timer1 interrupt

 byte i;
 byte for_me = 0;

 //RS232_Print("$ ");

 // check start bit
 TCNT0 = 0;
 while (INPUT_IS_SET) {
	if ( TCNT0 > 2286 ) { // Originally 254
		STARTEvent;
		RS232_Print("LAN>T1\n");
		return 0;
	}
 }


 if ( TCNT0 < 90 ) {		// !!!!!!! 20 !!!!!!!!!!! (Originally 10)
 	STARTEvent;
	RS232_Print("LAN>T2\n");
	return 0;
 }



 broadcast = AVCLan_Read_Byte(1);

 parity_bit = 0;
 master1 = AVCLan_Read_Byte(4);
 master2 = AVCLan_Read_Byte(8);
 if ((parity_bit&1)!=AVCLan_Read_Byte(1)) {
	STARTEvent;
	return 0;
 }

 parity_bit = 0;
 slave1 = AVCLan_Read_Byte(4);
 slave2 = AVCLan_Read_Byte(8);
 if ((parity_bit&1)!=AVCLan_Read_Byte(1)) {
	STARTEvent;
	return 0;
 }
 // is this command for me ?
 if ((slave1==CD_ID_1)&&(slave2==CD_ID_2)) {
 	for_me=1;
 }

 if (for_me) AVCLan_Send_ACK();
 		else AVCLan_Read_Byte(1);

 parity_bit = 0;
 AVCLan_Read_Byte(4);	// control - always 0xF
 if ((parity_bit&1)!=AVCLan_Read_Byte(1)) {
	STARTEvent;
	return 0;
 }
 if (for_me) AVCLan_Send_ACK();
 		else AVCLan_Read_Byte(1);

 parity_bit = 0;
 message_len = AVCLan_Read_Byte(8);
 if ((parity_bit&1)!=AVCLan_Read_Byte(1)) {
	STARTEvent;
	return 0;
 }
 if (for_me) AVCLan_Send_ACK();
 		else AVCLan_Read_Byte(1);

 if (message_len > MAXMSGLEN) {
//	RS232_Print("LAN> Command error");
	STARTEvent;
	return 0;
 }

 for (i=0; i<message_len; i++) {
	parity_bit = 0;
 	message[i] = AVCLan_Read_Byte(8);
	if ((parity_bit&1)!=AVCLan_Read_Byte(1)) {
		STARTEvent;
		return 0;
 	}
	if (for_me) {
		AVCLan_Send_ACK();
 	} else {
		AVCLan_Read_Byte(1);
	}
 }


 STARTEvent;

 if (showLog) ShowInMessage();

 if (for_me) {

	if (CheckCmd((byte*)stat1)) { answerReq = cmStatus1; return 1; }
	if (CheckCmd((byte*)stat2)) { answerReq = cmStatus2; return 1; }
	if (CheckCmd((byte*)stat3)) { answerReq = cmStatus3; return 1; }
	if (CheckCmd((byte*)stat4)) { answerReq = cmStatus4; return 1; }
//	if (CheckCmd((byte*)stat5)) { answerReq = cmStatus5; return 1; }

	if (CheckCmd((byte*)play_req1)) { answerReq = cmPlayReq1; return 1; }
	if (CheckCmd((byte*)play_req2)) { answerReq = cmPlayReq2; return 1; }
	if (CheckCmd((byte*)play_req3)) { answerReq = cmPlayReq3; return 1; }
	if (CheckCmd((byte*)stop_req))  { answerReq = cmStopReq;  return 1; }
	if (CheckCmd((byte*)stop_req2)) { answerReq = cmStopReq2; return 1; }

 } else { // broadcast check

	if (CheckCmd((byte*)lan_playit))	{ answerReq = cmPlayIt;	return 1; }
	if (CheckCmd((byte*)lan_check))	{
			answerReq = cmCheck;
			CMD_CHECK[6]=message[3];
			return 1;
	}
	if (CheckCmd((byte*)lan_reg))	{ answerReq = cmRegister;	return 1; }
	if (CheckCmd((byte*)lan_init))	{ answerReq = cmInit;		return 1; }
	if (CheckCmd((byte*)lan_stat1))	{ answerReq = cmStatus1;	return 1; }


 }
 answerReq = cmNull;
 return 1;
}

// DONE: Timing adjusted.
//------------------------------------------------------------------------------
byte AVCLan_SendData()
{
 byte i;

 STOPEvent;

 // wait for free line
 byte line_busy = 1;

 TCNT0 = 0;
 do {
 	while (INPUT_IS_CLEAR) {
		if ( TCNT0 >= 225 ) break;
 	}
 	if ( TCNT0 > 216 ) line_busy=0;
 } while (line_busy);


 // switch to output mode
 AVC_OUT_EN();

 AVCLan_Send_StartBit();
 AVCLan_Send_Byte(0x1,  1);		// regular communication


 parity_bit = 0;
 AVCLan_Send_Byte(CD_ID_1, 4);	// CD Changer ID as master
 AVCLan_Send_Byte(CD_ID_2, 8);
 AVCLan_Send_ParityBit();

 AVCLan_Send_Byte(HU_ID_1, 4);	// HeadUnit ID as slave
 AVCLan_Send_Byte(HU_ID_2, 8);

 AVCLan_Send_ParityBit();

 if (AVCLan_Read_ACK()) {
 	 AVC_OUT_DIS();
	 STARTEvent;
	 RS232_Print("E1\n");
	 return 1;
 }


 AVCLan_Send_Byte(0xF, 4);		// 0xf - control -> COMMAND WRITE
 AVCLan_Send_ParityBit();
 if (AVCLan_Read_ACK()) {
 	 AVC_OUT_DIS();
	 STARTEvent;
	 RS232_Print("E2\n");
	 return 2;
 }

 AVCLan_Send_Byte(data_len,  8);// data length
 AVCLan_Send_ParityBit();
 if (AVCLan_Read_ACK()) {
 	 AVC_OUT_DIS();
	 STARTEvent;
	 RS232_Print("E3\n");
	 return 3;
 }

 for (i=0;i<data_len;i++) {
	AVCLan_Send_Byte(data[i], 8);// data byte
 	AVCLan_Send_ParityBit();
 	if (AVCLan_Read_ACK()) {
	 	 AVC_OUT_DIS();
		 STARTEvent;
 		 RS232_Print("E4(");
		 RS232_PrintDec(i);
		 RS232_Print(")\n");
		 return 4;
 	}
 }

 // back to read mode
 AVC_OUT_DIS();

 STARTEvent;
 if (showLog) ShowOutMessage();
 return 0;
}

// DONE: Timing adjusted.
//------------------------------------------------------------------------------
byte AVCLan_SendDataBroadcast()
{
	byte i;

	STOPEvent;

	// wait for free line
	byte line_busy = 1;

	TCNT0 = 0;
	do {
		while (INPUT_IS_CLEAR) {
		if ( TCNT0 >= 225 ) break;
		}
		if ( TCNT0 > 216 ) line_busy=0;
	} while (line_busy);


	AVC_OUT_EN();

	AVCLan_Send_StartBit();
	AVCLan_Send_Byte(0x0,  1);		// broadcast

	parity_bit = 0;
	AVCLan_Send_Byte(CD_ID_1, 4);	// CD Changer ID as master
	AVCLan_Send_Byte(CD_ID_2, 8);
	AVCLan_Send_ParityBit();

	AVCLan_Send_Byte(0x1, 4);		// all audio devices
	AVCLan_Send_Byte(0xFF, 8);
	AVCLan_Send_ParityBit();
	AVCLan_Send_Bit1();

	AVCLan_Send_Byte(0xF, 4);		// 0xf - control -> COMMAND WRITE
	AVCLan_Send_ParityBit();
	AVCLan_Send_Bit1();

	AVCLan_Send_Byte(data_len,  8);	// data lenght
	AVCLan_Send_ParityBit();
	AVCLan_Send_Bit1();

	for (i=0;i<data_len;i++) {
	AVCLan_Send_Byte(data[i], 8); // data byte
		AVCLan_Send_ParityBit();
	AVCLan_Send_Bit1();
	}

	AVC_OUT_DIS();
	STARTEvent;
	if (showLog) ShowOutMessage();
	return 0;
}

// DONE: Nothing needed.
//------------------------------------------------------------------------------
byte AVCLan_SendAnswerFrame(byte *cmd)
{
	byte i;
	byte *c;
	byte b;

	c = cmd;

	b = *c++;
	data_control = 0xF;
	data_len	 = *c++;
	for (i=0; i<data_len; i++) {
		data[i]= *c++;
	}
	if (b)
		return AVCLan_SendData();
	else
		return AVCLan_SendDataBroadcast();
}

// DONE: Nothing needed.
//------------------------------------------------------------------------------
byte AVCLan_SendMyData(byte *data_tmp, byte s_len)
{
	byte i;
	byte *c;

	c = data_tmp;

	data_control = 0xF;
	data_len	 = s_len;
	for (i=0; i<data_len; i++) {
		data[i]= *c++;
	}
	return AVCLan_SendData();
}

// DONE: Nothing needed.
//------------------------------------------------------------------------------
byte AVCLan_SendMyDataBroadcast(byte *data_tmp, byte s_len)
{
	byte i;
	byte *c;


	c = data_tmp;

	data_control = 0xF;
	data_len	 = s_len;
	for (i=0; i<data_len; i++) {
		data[i]= *c++;
	}
	return AVCLan_SendDataBroadcast();
}

// DONE: Nothing needed.
//------------------------------------------------------------------------------
byte AVCLan_SendInitCommands()
{
	byte r;

	const byte c1[] = { 0x0, 0x0B, 0x63, 0x31, 0xF1, 0x00, 0x80, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x80 };
	const byte c2[] = { 0x0, 0x0A, 0x63, 0x31, 0xF3, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x02 };
	const byte c3[] = { 0x0, 0x0A, 0x63, 0x31, 0xF3, 0x00, 0x3F, 0x00, 0x01, 0x00, 0x01, 0x02 };
	const byte c4[] = { 0x0, 0x0A, 0x63, 0x31, 0xF3, 0x00, 0x3D, 0x00, 0x01, 0x00, 0x01, 0x02 };
	const byte c5[] = { 0x0, 0x0A, 0x63, 0x31, 0xF3, 0x00, 0x39, 0x00, 0x01, 0x00, 0x01, 0x02 };
	const byte c6[] = { 0x0, 0x0A, 0x63, 0x31, 0xF3, 0x00, 0x31, 0x00, 0x01, 0x00, 0x01, 0x02 };
	const byte c7[] = { 0x0, 0x0A, 0x63, 0x31, 0xF3, 0x00, 0x21, 0x00, 0x01, 0x00, 0x01, 0x02 };
	const byte c8[] = { 0x0, 0x0B, 0x63, 0x31, 0xF1, 0x00, 0x90, 0x01, 0xFF, 0xFF, 0xFF, 0x00, 0x80 };
	const byte c9[] = { 0x0, 0x0A, 0x63, 0x31, 0xF3, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x02 };
	const byte cA[] = { 0x0, 0x0B, 0x63, 0x31, 0xF1, 0x00, 0x30, 0x01, 0xFF, 0xFF, 0xFF, 0x00, 0x80 };

	r = AVCLan_SendAnswerFrame((byte*)c1);
	if (!r) r = AVCLan_SendAnswerFrame((byte*)c2);
	if (!r) r = AVCLan_SendAnswerFrame((byte*)c3);
	if (!r) r = AVCLan_SendAnswerFrame((byte*)c4);
	if (!r) r = AVCLan_SendAnswerFrame((byte*)c5);
	if (!r) r = AVCLan_SendAnswerFrame((byte*)c6);
	if (!r) r = AVCLan_SendAnswerFrame((byte*)c7);
	if (!r) r = AVCLan_SendAnswerFrame((byte*)c8);
	if (!r) r = AVCLan_SendAnswerFrame((byte*)c9);
	if (!r) r = AVCLan_SendAnswerFrame((byte*)cA);

	//const byte c1[] = { 0x0, 0x0B,		0x63, 0x31, 0xF1, 0x00, 0x80, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x80 };
	//r = AVCLan_SendAnswerFrame((byte*)c1);
	return r;
}

// DONE: Nothing needed.
//------------------------------------------------------------------------------
void AVCLan_Send_Status()
{
	//                                                        disc  track t_min t_sec
	 byte STATUS[] = {0x0, 0x0B, 0x63, 0x31, 0xF1, 0x01, 0x10, 0x01, 0x01, 0x00, 0x00, 0x00, 0x80 };

	 STATUS[7] =  cd_Disc;
	 STATUS[8] =  cd_Track;
	 STATUS[9] =  cd_Time_Min;
	 STATUS[10] = cd_Time_Sec;

	 STATUS[11] = 0;

	 AVCLan_SendAnswerFrame((byte*)STATUS);
}

// DONE: Nothing needed.
//------------------------------------------------------------------------------
byte AVCLan_SendAnswer()
{
 byte r = 0 ;

 switch (answerReq) {
 	case cmStatus1:		r = AVCLan_SendAnswerFrame((byte*)CMD_STATUS1);
						break;
 	case cmStatus2:		r = AVCLan_SendAnswerFrame((byte*)CMD_STATUS2);
						break;
 	case cmStatus3:		r = AVCLan_SendAnswerFrame((byte*)CMD_STATUS3);
						break;
 	case cmStatus4:		r = AVCLan_SendAnswerFrame((byte*)CMD_STATUS4);
						break;
 	case cmRegister:	r = AVCLan_SendAnswerFrame((byte*)CMD_REGISTER);
						break;
 	case cmInit:		//RS232_Print("INIT\n");
						r = AVCLan_SendInitCommands();
						break;
 	case cmCheck:		r = AVCLan_SendAnswerFrame((byte*)CMD_CHECK);
						check_timeout = 0;
						CMD_CHECK[6]++;
 						RS232_Print("AVCCHK\n");
						break;
 	case cmPlayReq1:	playMode = 0;
						r = AVCLan_SendAnswerFrame((byte*)CMD_PLAY_OK1);
						break;
 	case cmPlayReq2:
	case cmPlayReq3:	playMode = 0;
						r = AVCLan_SendAnswerFrame((byte*)CMD_PLAY_OK2);
						if (!r) r = AVCLan_SendAnswerFrame((byte*)CMD_PLAY_OK3);
						CD_Mode = stPlay;
						break;
	case cmPlayIt:		playMode = 1;
						RS232_Print("PLAY\n");
						CMD_PLAY_OK4[7]=cd_Disc;
						CMD_PLAY_OK4[8]=cd_Track;
						CMD_PLAY_OK4[9]=cd_Time_Min;
						CMD_PLAY_OK4[10]=cd_Time_Sec;
						r = AVCLan_SendAnswerFrame((byte*)CMD_PLAY_OK4);
						if (!r) AVCLan_Send_Status();
						CD_Mode = stPlay;
						break;
	case cmStopReq:
	case cmStopReq2:	CD_Mode = stStop;
						playMode = 0;

						r = AVCLan_SendAnswerFrame((byte*)CMD_STOP1);
						CMD_STOP2[7]=cd_Disc;
						CMD_STOP2[8]=cd_Track;
						CMD_STOP2[9]=cd_Time_Min;
						CMD_STOP2[10]=cd_Time_Sec;
						r = AVCLan_SendAnswerFrame((byte*)CMD_STOP2);
						break;
	case cmBeep:		AVCLan_SendAnswerFrame((byte*)CMD_BEEP);
						break;
 }

 answerReq = cmNull;
 return r;
}

// DONE: Nothing needed.
//------------------------------------------------------------------------------
void AVCLan_Register()
{
 RS232_Print("REG_ST\n");
 AVCLan_SendAnswerFrame((byte*)CMD_REGISTER);
 RS232_Print("REG_END\n");
 //AVCLan_Command( cmRegister );
 AVCLan_Command( cmInit );
}

// DONE: Nothing needed.
//------------------------------------------------------------------------------
byte AVCLan_Command(byte command)
{
 byte r;

 answerReq = command;
 r = AVCLan_SendAnswer();
 /*
 RS232_Print("ret=");
 RS232_PrintHex8(r);
 RS232_Print("\n");
 */
 return r;
}

// DONE: Nothing needed.
//------------------------------------------------------------------------------
byte HexInc(byte data)
{
 if ((data & 0x9)==0x9)
 	return (data + 7);

 return (data+1);
}

// DONE: Nothing needed.
//------------------------------------------------------------------------------
byte HexDec(byte data)
{
 if ((data & 0xF)==0)
 	return (data - 7);

 return (data-1);
}

// DONE: Nothing needed.
//------------------------------------------------------------------------------
// encode decimal valute to 'toyota' format :-)
//  ex.   42 (dec)   =  0x42 (toy)
byte Dec2Toy(byte data)
{
 byte d,d1;
 d = (u32)data/(u32)10;
 d1 = d * 16;
 d  = d1 + (data - 10*d);
 return d;
}

// DONE: No timing adjustment needed.
//------------------------------------------------------------------------------
void ShowInMessage()
{
 if (message_len==0) return;

 AVC_HoldLine();


 RS232_S((u16)PSTR("HU < ("));

 if (broadcast==0) RS232_S((u16)PSTR("bro) "));
 else RS232_Print("dir) ");

 RS232_PrintHex4(master1);
 RS232_PrintHex8(master2);
 RS232_Print("| ");
 RS232_PrintHex4(slave1);
 RS232_PrintHex8(slave2);
 RS232_Print("| ");

 byte i;
 for (i=0;i<message_len;i++) {
	RS232_PrintHex8(message[i]);
	RS232_Print(" ");
 }
 RS232_Print("\n");

 AVC_ReleaseLine();
}

//------------------------------------------------------------------------------
void ShowOutMessage()
{
 byte i;

 AVC_HoldLine();

 RS232_S((u16)PSTR("out > "));
 for (i=0; i<data_len; i++) {
	RS232_PrintHex8(data[i]);
	RS232_SendByte(' ');
 }
 RS232_Print("\n");

 AVC_ReleaseLine();
}

//------------------------------------------------------------------------------
