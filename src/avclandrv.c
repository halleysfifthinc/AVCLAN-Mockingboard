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

#include <avr/interrupt.h>
#include <avr/io.h>

#include "GlobalDef.h"
#include "avclandrv.h"
#include "com232.h"

// Enable AVC bus Tx
#define AVC_OUT_EN()                                                           \
  cbi(AC2_CTRLA, AC_ENABLE_bp);                                                \
  sbi(VPORTA_DIR, 6);

// Disable AVC bus Tx
#define AVC_OUT_DIS()                                                          \
  cbi(VPORTA_DIR, 6);                                                          \
  sbi(AC2_CTRLA, AC_ENABLE_bp);

#define AVC_SET_LOGICAL_1()                                                    \
  __asm__ __volatile__(                                                        \
      "sbi %[vporta_out], 6;" ::[vporta_out] "I"(_SFR_IO_ADDR(VPORTA_OUT)));
#define AVC_SET_LOGICAL_0()                                                    \
  __asm__ __volatile__(                                                        \
      "cbi %[vporta_out], 6;" ::[vporta_out] "I"(_SFR_IO_ADDR(VPORTA_OUT)));

// Name difference between avr-libc and Microchip pack
#if defined(EVSYS_ASYNCCH00_bm)
#define EVSYS_ASYNCCH0_0_bm EVSYS_ASYNCCH00_bm
#endif

uint16_t CD_ID;
uint16_t HU_ID;

uint8_t repeatMode;
uint8_t randomMode;

uint8_t playMode;

uint8_t cd_Disc;
uint8_t cd_Track;
uint8_t cd_Time_Min;
uint8_t cd_Time_Sec;

uint8_t answerReq;

cd_modes CD_Mode;

// we need check answer (to avclan check) timeout
// when is more then 1 min, FORCE answer.
uint8_t check_timeout;

#define SW_ID 0x11 // 11 For my stereo

// commands
const uint8_t stat1[] = {0x4, 0x00, 0x00, 0x01, 0x0A};
const uint8_t stat2[] = {0x4, 0x00, 0x00, 0x01, 0x08};
const uint8_t stat3[] = {0x4, 0x00, 0x00, 0x01, 0x0D};
const uint8_t stat4[] = {0x4, 0x00, 0x00, 0x01, 0x0C};

// broadcast
const uint8_t lan_stat1[] = {0x3, 0x00, 0x01, 0x0A};
const uint8_t lan_reg[] = {0x3, SW_ID, 0x01, 0x00};
const uint8_t lan_init[] = {0x3, SW_ID, 0x01, 0x01};
const uint8_t lan_check[] = {0x3, SW_ID, 0x01, 0x20};
const uint8_t lan_playit[] = {0x4, SW_ID, 0x01, 0x45, 0x63};

const uint8_t play_req1[] = {0x4, 0x00, 0x25, 0x63, 0x80};

#ifdef __AVENSIS__
const uint8_t play_req2[] = {0x6, 0x00, SW_ID, 0x63, 0x42};
#else
const uint8_t play_req2[] = {0x6, 0x00, SW_ID, 0x63, 0x42, 0x01, 0x00};
#endif

const uint8_t play_req3[] = {0x5, 0x00, SW_ID, 0x63, 0x42, 0x41};
const uint8_t stop_req[] = {0x5, 0x00, SW_ID, 0x63, 0x43, 0x01};
const uint8_t stop_req2[] = {0x5, 0x00, SW_ID, 0x63, 0x43, 0x41};

// Init commands
const AVCLAN_KnownMessage_t c8 = {
    BROADCAST,
    11,
    {0x63, 0x31, 0xF1, 0x00, 0x90, 0x01, 0xFF, 0xFF, 0xFF, 0x00, 0x80}};
const AVCLAN_KnownMessage_t c1 = {
    BROADCAST,
    10,
    {0x63, 0x31, 0xF1, 0x00, 0x80, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x80}};
const AVCLAN_KnownMessage_t cA = {
    BROADCAST,
    11,
    {0x63, 0x31, 0xF1, 0x00, 0x30, 0x01, 0xFF, 0xFF, 0xFF, 0x00, 0x80}};
const AVCLAN_KnownMessage_t c2 = {
    BROADCAST,
    10,
    {0x63, 0x31, 0xF3, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x02}};
const AVCLAN_KnownMessage_t c3 = {
    BROADCAST,
    10,
    {0x63, 0x31, 0xF3, 0x00, 0x3F, 0x00, 0x01, 0x00, 0x01, 0x02}};
const AVCLAN_KnownMessage_t c4 = {
    BROADCAST,
    10,
    {0x63, 0x31, 0xF3, 0x00, 0x3D, 0x00, 0x01, 0x00, 0x01, 0x02}};
const AVCLAN_KnownMessage_t c5 = {
    BROADCAST,
    10,
    {0x63, 0x31, 0xF3, 0x00, 0x39, 0x00, 0x01, 0x00, 0x01, 0x02}};
const AVCLAN_KnownMessage_t c6 = {
    BROADCAST,
    10,
    {0x63, 0x31, 0xF3, 0x00, 0x31, 0x00, 0x01, 0x00, 0x01, 0x02}};
const AVCLAN_KnownMessage_t c7 = {
    BROADCAST,
    10,
    {0x63, 0x31, 0xF3, 0x00, 0x21, 0x00, 0x01, 0x00, 0x01, 0x02}};
const AVCLAN_KnownMessage_t c9 = {
    BROADCAST,
    10,
    {0x63, 0x31, 0xF3, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x02}};

// answers
const AVCLAN_KnownMessage_t CMD_REGISTER = {
    UNICAST, 5, {0x00, 0x01, SW_ID, 0x10, 0x63}};
const AVCLAN_KnownMessage_t CMD_STATUS1 = {
    UNICAST, 4, {0x00, 0x01, 0x00, 0x1A}};
const AVCLAN_KnownMessage_t CMD_STATUS2 = {
    UNICAST, 4, {0x00, 0x01, 0x00, 0x18}};
const AVCLAN_KnownMessage_t CMD_STATUS3 = {
    UNICAST, 4, {0x00, 0x01, 0x00, 0x1D}};
const AVCLAN_KnownMessage_t CMD_STATUS4 = {
    UNICAST, 5, {0x00, 0x01, 0x00, 0x1C, 0x00}};
AVCLAN_KnownMessage_t CMD_CHECK = {
    UNICAST, 6, {0x00, 0x01, SW_ID, 0x30, 0x00, 0x00}};

const AVCLAN_KnownMessage_t CMD_STATUS5 = {
    UNICAST, 5, {0x00, 0x5C, 0x12, 0x53, 0x02}};
const AVCLAN_KnownMessage_t CMD_STATUS5A = {
    BROADCAST, 5, {0x5C, 0x31, 0xF1, 0x00, 0x00}};

const AVCLAN_KnownMessage_t CMD_STATUS6 = {
    UNICAST, 6, {0x00, 0x5C, 0x32, 0xF0, 0x02, 0x00}};

const AVCLAN_KnownMessage_t CMD_PLAY_OK1 = {
    UNICAST, 5, {0x00, 0x63, SW_ID, 0x50, 0x01}};
const AVCLAN_KnownMessage_t CMD_PLAY_OK2 = {
    UNICAST, 5, {0x00, 0x63, SW_ID, 0x52, 0x01}};
const AVCLAN_KnownMessage_t CMD_PLAY_OK3 = {
    BROADCAST,
    11,
    {0x63, 0x31, 0xF1, 0x01, 0x00, 0x01, 0xFF, 0xFF, 0xFF, 0x00, 0x80}};
AVCLAN_KnownMessage_t CMD_PLAY_OK4 = {
    BROADCAST,
    11,
    {0x63, 0x31, 0xF1, 0x01, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80}};

const AVCLAN_KnownMessage_t CMD_STOP1 = {
    UNICAST, 5, {0x00, 0x63, SW_ID, 0x53, 0x01}};
AVCLAN_KnownMessage_t CMD_STOP2 = {
    BROADCAST,
    11,
    {0x63, 0x31, 0xF1, 0x00, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80}};

const AVCLAN_KnownMessage_t CMD_BEEP = {
    UNICAST, 5, {0x00, 0x63, 0x29, 0x60, 0x02}};

void AVC_HoldLine() {
  STOPEvent;

  // wait for free line
  uint8_t line_busy = 1;

  TCB1.CNT = 0;
  do {
    while (INPUT_IS_CLEAR) {
      /*	The comparison value was originally 25 with CK64 (tick period
         of 4.34 us) at a clock frequency 14.7456MHz. For a more accurate tick
         period of .5 us at 16MHz, the value should be approximately 225*/
      if (TCB1.CNT >= 900)
        break;
    }
    if (TCB1.CNT > 864)
      line_busy = 0;
  } while (line_busy);

  // switch to out mode
  AVC_OUT_EN();
  AVC_SET_LOGICAL_1();

  STARTEvent;
}

void AVC_ReleaseLine() {
  AVC_SET_LOGICAL_0();
  AVC_OUT_DIS();
}

void AVCLan_Init() {
  PORTA.PIN6CTRL = PORT_ISC_INPUT_DISABLE_gc; // Disable input buffer;
  PORTA.PIN7CTRL = PORT_ISC_INPUT_DISABLE_gc; // recommended when using AC

  // Pull-ups are disabled by default
  // Set pin 6 and 7 as input
  PORTA.DIRCLR = (PIN6_bm | PIN7_bm);

  // Analog comparator
  AC2.CTRLA = AC_OUTEN_bm | AC_HYSMODE_25mV_gc | AC_ENABLE_bm;
  PORTB.DIRSET = PIN2_bm; // Enable AC2 OUT for LED

  TCB1.CTRLB = TCB_ASYNC_bm | TCB_CNTMODE_SINGLE_gc;
  TCB1.EVCTRL = TCB_CAPTEI_bm;
  TCB1.INTCTRL = TCB_CAPT_bm;
  EVSYS.ASYNCUSER0 = EVSYS_ASYNCUSER0_ASYNCCH0_gc;
  TCB1.CTRLA = TCB_CLKSEL_CLKDIV2_gc | TCB_ENABLE_bm;

  answerReq = cm_Null;
  check_timeout = 0;

  cd_Disc = 1;
  cd_Track = 1;
  cd_Time_Min = 0;
  cd_Time_Sec = 0;
  repeatMode = 0;
  randomMode = 0;
  playMode = 0;
  CD_Mode = stStop;
}

uint8_t AVCLan_Read_Byte(uint8_t length, uint8_t *parity) {
  uint8_t byte = 0;

  while (1) {
    while (INPUT_IS_CLEAR) {}
    TCB1.CNT = 0;
    while (INPUT_IS_SET) {} // If input was set for less than 26 us
    if (TCB1.CNT < 208) {   // (a generous half period), bit was a 1
      byte++;
      (*parity)++;
    }
    length--;
    if (!length)
      return byte;
    byte = byte << 1;
  }
}

void set_AVC_logic_for(uint8_t val, uint16_t period) {
  if (val == 1) {
    AVC_SET_LOGICAL_1();
  } else {
    AVC_SET_LOGICAL_0();
  }
  TCB1.CCMP = period;
  EVSYS.ASYNCSTROBE = EVSYS_ASYNCCH0_0_bm;
  loop_until_bit_is_set(TCB1_INTFLAGS, 0);
  TCB1_INTFLAGS = 1;
}

uint8_t AVCLan_Send_StartBit() {
  set_AVC_logic_for(1, 1328); // 166 us @ 125 ns tick (for F_CPU = 16MHz)
  set_AVC_logic_for(0, 152);  // 19 us @ 125 ns tick (for F_CPU = 16MHz)

  return 1;
}

void AVCLan_Send_Bit1() {
  set_AVC_logic_for(1, 164); // 20.5 us @ 125 ns tick (for F_CPU = 16MHz)
  set_AVC_logic_for(0, 152); // 19 us @ 125 ns tick (for F_CPU = 16MHz)
}

void AVCLan_Send_Bit0() {
  set_AVC_logic_for(1, 272); // 34 us @ 125 ns tick (for F_CPU = 16MHz)
  set_AVC_logic_for(0, 44);  // 5.5 us @ 125 ns tick (for F_CPU = 16MHz)
}

uint8_t AVCLan_Read_ACK() {
  set_AVC_logic_for(1, 152); // 34 us @ 125 ns tick (for F_CPU = 16MHz)
  AVC_SET_LOGICAL_0();       // Replace with AVC_ReleaseLine?
  AVC_OUT_DIS();             // switch to read mode

  TCB1.CNT = 0;
  while (1) {
    if (INPUT_IS_SET && (TCB1.CNT > 208))
      break; // Make sure INPUT is not still set from us
    // Line of experimentation: Try changing TCNT0 comparison value or remove
    // check entirely
    if (TCB1.CNT > 300)
      return 1; // Not sure if this fix is intent correct
  }

  while (INPUT_IS_SET) {}
  AVC_OUT_EN(); // back to write mode
  return 0;
}

uint8_t AVCLan_Send_ACK() {
  TCB1.CNT = 0;
  while (INPUT_IS_CLEAR) {
    if (TCB1.CNT >= 900)
      return 0; // max wait time
  }

  AVC_OUT_EN();

  set_AVC_logic_for(1, 272); // 34 us @ 125 ns tick (for F_CPU = 16MHz)
  set_AVC_logic_for(0, 44);  // 5.5 us @ 125 ns tick (for F_CPU = 16MHz)

  AVC_OUT_DIS();

  return 1;
}

#define AVCLAN_sendbits(bits, len)                                             \
  _Generic((bits),                                                             \
      const uint16_t *: AVCLAN_sendbitsl,                                      \
      uint16_t *: AVCLAN_sendbitsl,                                            \
      const uint8_t *: AVCLAN_sendbitsi,                                       \
      uint8_t *: AVCLAN_sendbitsi)(bits, len)

// Send `len` bits on the AVCLAN bus; returns the even parity
uint8_t AVCLAN_sendbitsi(const uint8_t *byte, int8_t len) {
  uint8_t b = *byte;
  uint8_t parity = 0;
  int8_t len_mod8 = 8;

  if (len & 0x7) {
    len_mod8 = len & 0x7;
    b <<= (uint8_t)(8 - len_mod8);
  }

  while (len > 0) {
    len -= len_mod8;
    for (; len_mod8 > 0; len_mod8--) {
      if (b & 0x80) {
        AVCLan_Send_Bit1();
        parity++;
      } else {
        AVCLan_Send_Bit0();
      }
      b <<= 1;
    }
    len_mod8 = 8;
    b = *--byte;
  }
  return (parity & 1);
}

// Send `len` bits on the AVCLAN bus; returns the even parity
uint8_t AVCLAN_sendbitsl(const uint16_t *word, int8_t len) {
  return AVCLAN_sendbitsi((const uint8_t *)word + 1, len);
}

uint8_t AVCLAN_sendbyte(const uint8_t *byte) {
  uint8_t b = *byte;
  uint8_t parity = 0;

  for (uint8_t nbits = 8; nbits > 0; nbits--) {
    if (b & 0x80) {
      AVCLan_Send_Bit1();
      parity++;
    } else {
      AVCLan_Send_Bit0();
    }
    b <<= 1;
  }
  return (parity & 1);
}

void AVCLan_Send_ParityBit(uint8_t parity) {
  if (parity) {
    AVCLan_Send_Bit1();
  } else {
    AVCLan_Send_Bit0();
  }
}

uint8_t CheckCmd(const AVCLAN_frame_t *frame, const uint8_t *cmd) {
  uint8_t l = *cmd++;

  for (uint8_t i = 0; i < l; i++) {
    if (frame->data[i] != *cmd++)
      return 0;
  }
  return 1;
}

uint8_t AVCLAN_readframe() {
  STOPEvent; // disable timer1 interrupt

  uint8_t i;
  uint8_t for_me = 0;
  AVCLAN_frame_t frame = {};

  // RS232_Print("$ ");
  //  TCCR1B |= (1 << CS11)|(1 << CS10); // Timer1 prescaler at 64
  //  TCNT1 = 0;
  //  TCNT0 = 0;
  //  while (INPUT_IS_SET) {
  //  if ( TCNT0 > 255 ) { // 170 us
  //  	// TCCR1B = 0;
  //  	// TCCR1B |= (1 << WGM12)|(1 << CS12); // Set CTC, prescaler at 256
  //  	STARTEvent;
  //  	RS232_Print("LAN>T1\n");
  //  	return 0;
  //  }
  //  }
  //
  //  if ( TCNT0 < 20 ) {		// 20 us
  //  	// TCCR1B = 0;
  //  	// TCCR1B |= (1 << WGM12)|(1 << CS12);
  //  	STARTEvent;
  //  	RS232_Print("LAN>T2\n");
  //  	return 0;
  //  }
  uint8_t parity = 0;
  uint8_t parity_check = 0;
  AVCLan_Read_Byte(1, &parity); // Start bit

  frame.broadcast = AVCLan_Read_Byte(1, &parity);

  parity = 0;
  uint8_t *sender_hi = ((uint8_t *)&frame.sender_addr) + 1;
  uint8_t *sender_lo = ((uint8_t *)&frame.sender_addr) + 0;
  *sender_hi = AVCLan_Read_Byte(4, &parity);
  *sender_lo = AVCLan_Read_Byte(8, &parity);
  if ((parity & 1) != AVCLan_Read_Byte(1, &parity_check)) {
    STARTEvent;
    return 0;
  }

  parity = 0;
  uint8_t *responder_hi = ((uint8_t *)&frame.responder_addr) + 1;
  uint8_t *responder_lo = ((uint8_t *)&frame.responder_addr) + 0;
  *responder_hi = AVCLan_Read_Byte(4, &parity);
  *responder_lo = AVCLan_Read_Byte(8, &parity);
  if ((parity & 1) != AVCLan_Read_Byte(1, &parity_check)) {
    STARTEvent;
    return 0;
  }

  // is this command for me ?
  for_me = (frame.responder_addr == CD_ID);

  if (for_me)
    AVCLan_Send_ACK();
  else
    AVCLan_Read_Byte(1, &parity);

  parity = 0;
  frame.control = AVCLan_Read_Byte(4, &parity);
  if ((parity & 1) != AVCLan_Read_Byte(1, &parity_check)) {
    STARTEvent;
    return 0;
  } else if (for_me) {
    AVCLan_Send_ACK();
  } else {
    AVCLan_Read_Byte(1, &parity);
  }

  parity = 0;
  frame.length = AVCLan_Read_Byte(8, &parity);
  if ((parity & 1) != AVCLan_Read_Byte(1, &parity_check)) {
    STARTEvent;
    return 0;
  } else if (for_me) {
    AVCLan_Send_ACK();
  } else {
    AVCLan_Read_Byte(1, &parity);
  }

  if (frame.length > MAXMSGLEN) {
    //	RS232_Print("LAN> Command error");
    STARTEvent;
    return 0;
  }

  for (i = 0; i < frame.length; i++) {
    parity = 0;
    frame.data[i] = AVCLan_Read_Byte(8, &parity);
    if ((parity & 1) != AVCLan_Read_Byte(1, &parity_check)) {
      STARTEvent;
      return 0;
    } else if (for_me) {
      AVCLan_Send_ACK();
    } else {
      AVCLan_Read_Byte(1, &parity);
    }
  }

  STARTEvent;

  if (showLog)
    AVCLAN_printframe(&frame);

  if (for_me) {

    if (CheckCmd(&frame, stat1)) {
      answerReq = cm_Status1;
      return 1;
    }
    if (CheckCmd(&frame, stat2)) {
      answerReq = cm_Status2;
      return 1;
    }
    if (CheckCmd(&frame, stat3)) {
      answerReq = cm_Status3;
      return 1;
    }
    if (CheckCmd(&frame, stat4)) {
      answerReq = cm_Status4;
      return 1;
    }
    //	if (CheckCmd((uint8_t*)stat5)) { answerReq = cm_Status5; return 1; }

    if (CheckCmd(&frame, play_req1)) {
      answerReq = cm_PlayReq1;
      return 1;
    }
    if (CheckCmd(&frame, play_req2)) {
      answerReq = cm_PlayReq2;
      return 1;
    }
    if (CheckCmd(&frame, play_req3)) {
      answerReq = cm_PlayReq3;
      return 1;
    }
    if (CheckCmd(&frame, stop_req)) {
      answerReq = cm_StopReq;
      return 1;
    }
    if (CheckCmd(&frame, stop_req2)) {
      answerReq = cm_StopReq2;
      return 1;
    }

  } else { // broadcast check

    if (CheckCmd(&frame, lan_playit)) {
      answerReq = cm_PlayIt;
      return 1;
    }
    if (CheckCmd(&frame, lan_check)) {
      answerReq = cm_Check;
      CMD_CHECK.data[4] = frame.data[3];
      return 1;
    }
    if (CheckCmd(&frame, lan_reg)) {
      answerReq = cm_Register;
      return 1;
    }
    if (CheckCmd(&frame, lan_init)) {
      answerReq = cm_Init;
      return 1;
    }
    if (CheckCmd(&frame, lan_stat1)) {
      answerReq = cm_Status1;
      return 1;
    }
  }
  answerReq = cm_Null;
  return 1;
}

uint8_t AVCLAN_sendframe(const AVCLAN_frame_t *frame) {
  STOPEvent;

  // wait for free line
  uint8_t line_busy = 1;
  uint8_t parity = 0;

  TCB1.CNT = 0;
  do {
    while (INPUT_IS_CLEAR) {
      if (TCB1.CNT >= 900)
        break;
    }
    if (TCB1.CNT > 864)
      line_busy = 0;
  } while (line_busy);

  // switch to output mode
  AVC_OUT_EN();

  AVCLan_Send_StartBit();
  AVCLAN_sendbits((uint8_t *)&frame->broadcast, 1);

  parity = AVCLAN_sendbits(&frame->sender_addr, 12);
  AVCLan_Send_ParityBit(parity);

  parity = AVCLAN_sendbits(&frame->responder_addr, 12);
  AVCLan_Send_ParityBit(parity);

  if (!frame->broadcast && AVCLan_Read_ACK()) {
    AVC_OUT_DIS();
    STARTEvent;
    RS232_Print("Error NAK: Responder\n");
    return 1;
  }

  parity = AVCLAN_sendbits(&frame->control, 4);
  AVCLan_Send_ParityBit(parity);

  if (!frame->broadcast && AVCLan_Read_ACK()) {
    AVC_OUT_DIS();
    STARTEvent;
    RS232_Print("Error NAK: Control\n");
    return 2;
  }

  parity = AVCLAN_sendbyte(&frame->length); // data length
  AVCLan_Send_ParityBit(parity);

  if (!frame->broadcast && AVCLan_Read_ACK()) {
    AVC_OUT_DIS();
    STARTEvent;
    RS232_Print("Error NAK: Message length\n");
    return 3;
  }

  for (uint8_t i = 0; i < frame->length; i++) {
    parity = AVCLAN_sendbyte(&frame->data[i]);
    AVCLan_Send_ParityBit(parity);
    // Based on the µPD6708 datasheet, ACK bit for broadcast doesn't seem
    // necessary (i.e. This deviates from the previous broadcast specific
    // function that sent an extra `1` bit after each byte/parity)
    if (!frame->broadcast && AVCLan_Read_ACK()) {
      AVC_OUT_DIS();
      STARTEvent;
      RS232_Print("Error ACK 4 (Data uint8_t: ");
      RS232_PrintDec(i);
      RS232_Print(")\n");
      return 4;
    }
  }

  // back to read mode
  AVC_OUT_DIS();
  STARTEvent;

  if (showLog)
    AVCLAN_printframe(frame);

  return 0;
}

// uint8_t AVCLan_SendDataBroadcast() {
//   uint8_t i;

//   STOPEvent;

//   // wait for free line
//   uint8_t line_busy = 1;

//   TCB1.CNT = 0;
//   do {
//     while (INPUT_IS_CLEAR) {
//       if (TCB1.CNT >= 900)
//         break;
//     }
//     if (TCB1.CNT > 864)
//       line_busy = 0;
//   } while (line_busy);

//   AVC_OUT_EN();

//   AVCLan_Send_StartBit();
//   uint8_t broadcast_control = 0x0;
//   AVCLAN_sendbits(&broadcast_control, 1); // broadcast

//   uint8_t parity = 0;
//   AVCLAN_sendbits(&CD_ID, 12); // CD Changer ID as sender
//   AVCLan_Send_ParityBit(parity);

//   uint16_t audio_addr = 0x1FF;
//   AVCLAN_sendbits(&audio_addr, 12); // all audio devices
//   AVCLan_Send_ParityBit(parity);
//   AVCLan_Send_Bit1();

//   broadcast_control = 0xF;
//   AVCLAN_sendbits(&broadcast_control, 4); // 0xf - control -> COMMAND WRITE
//   AVCLan_Send_ParityBit(parity);
//   AVCLan_Send_Bit1();

//   AVCLAN_sendbyte(&data_len); // data lenght
//   AVCLan_Send_ParityBit(parity);
//   AVCLan_Send_Bit1();

//   for (i = 0; i < data_len; i++) {
//     AVCLAN_sendbyte(&data[i]); // data uint8_t
//     AVCLan_Send_ParityBit(parity);
//     AVCLan_Send_Bit1();
//   }

//   AVC_OUT_DIS();
//   STARTEvent;
//   if (showLog)
//     ShowOutMessage();
//   return 0;
// }

// uint8_t AVCLan_SendAnswerFrame(const uint8_t *cmd) {
//   uint8_t i;
//   uint8_t b;

//   b = *cmd++;
//   data_control = 0xF;
//   data_len = *cmd++;
//   for (i = 0; i < data_len; i++) {
//     data[i] = *cmd++;
//   }
//   if (b)
//     return AVCLan_SendData();
//   else
//     return AVCLan_SendDataBroadcast();
// }

// uint8_t AVCLan_SendMyData(uint8_t *data_tmp, uint8_t s_len) {
//   uint8_t i;
//   uint8_t *c;

//   c = data_tmp;

//   data_control = 0xF;
//   data_len = s_len;
//   for (i = 0; i < data_len; i++) {
//     data[i] = *c++;
//   }
//   return AVCLan_SendData();
// }

// uint8_t AVCLan_SendMyDataBroadcast(uint8_t *data_tmp, uint8_t s_len) {
//   uint8_t i;
//   uint8_t *c;

//   c = data_tmp;

//   data_control = 0xF;
//   data_len = s_len;
//   for (i = 0; i < data_len; i++) {
//     data[i] = *c++;
//   }
//   return AVCLan_SendDataBroadcast();
// }

uint8_t AVCLan_SendInitCommands() {
  uint8_t r;
  AVCLAN_frame_t frame = {.broadcast = BROADCAST,
                          .sender_addr = CD_ID,
                          .responder_addr = HU_ID,
                          .control = 0xF,
                          .length = c1.length};
  frame.data = (uint8_t *)&c1.data[0];

  r = AVCLAN_sendframe(&frame);
  if (!r) {
    frame.length = c2.length;
    frame.data = (uint8_t *)&c2.data[0];
    r = AVCLAN_sendframe(&frame); // c2
  }
  if (!r) {
    frame.length = c3.length;
    frame.data = (uint8_t *)&c3.data[0];
    r = AVCLAN_sendframe(&frame); // c3
  }
  if (!r) {
    frame.length = c4.length;
    frame.data = (uint8_t *)&c4.data[0];
    r = AVCLAN_sendframe(&frame); // c4
  }
  if (!r) {
    frame.length = c5.length;
    frame.data = (uint8_t *)&c5.data[0];
    r = AVCLAN_sendframe(&frame); // c5
  }
  if (!r) {
    frame.length = c6.length;
    frame.data = (uint8_t *)&c6.data[0];
    r = AVCLAN_sendframe(&frame); // c6
  }
  if (!r) {
    frame.length = c7.length;
    frame.data = (uint8_t *)&c7.data[0];
    r = AVCLAN_sendframe(&frame); // c7
  }
  if (!r) {
    frame.length = c8.length;
    frame.data = (uint8_t *)&c8.data[0];
    r = AVCLAN_sendframe(&frame); // c8
  }
  if (!r) {
    frame.length = c9.length;
    frame.data = (uint8_t *)&c9.data[0];
    r = AVCLAN_sendframe(&frame); // c9
  }
  if (!r) {
    frame.length = cA.length;
    frame.data = (uint8_t *)&cA.data[0];
    r = AVCLAN_sendframe(&frame); // cA
  }
  // const uint8_t c1[] = { 0x0, 0x0B,		0x63, 0x31, 0xF1, 0x00, 0x80,
  // 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x80 }; r =
  // AVCLan_SendAnswerFrame((uint8_t*)c1);
  return r;
}

void AVCLan_Send_Status() {
  uint8_t STATUS[] = {0x63, 0x31, 0xF1, 0x01, 0x10, 0x01,
                      0x01, 0x00, 0x00, 0x00, 0x80};
  STATUS[5] = cd_Disc;
  STATUS[6] = cd_Track;
  STATUS[7] = cd_Time_Min;
  STATUS[8] = cd_Time_Sec;
  STATUS[9] = 0;

  AVCLAN_frame_t status = {.broadcast = UNICAST,
                           .sender_addr = CD_ID,
                           .responder_addr = HU_ID,
                           .control = 0xF,
                           .length = 11,
                           .data = &STATUS[0]};

  AVCLAN_sendframe(&status);
}

uint8_t AVCLan_SendAnswer() {
  uint8_t r = 0;
  AVCLAN_frame_t frame = {.broadcast = UNICAST,
                          .sender_addr = CD_ID,
                          .responder_addr = HU_ID,
                          .control = 0xF,
                          .length = 0};

  switch (answerReq) {
    case cm_Status1:
      frame.broadcast = CMD_STATUS1.broadcast;
      frame.length = CMD_STATUS1.length;
      frame.data = (uint8_t *)&CMD_STATUS1.data[0];
      r = AVCLAN_sendframe(&frame);
      break;
    case cm_Status2:
      frame.broadcast = CMD_STATUS2.broadcast;
      frame.length = CMD_STATUS2.length;
      frame.data = (uint8_t *)&CMD_STATUS2.data[0];
      r = AVCLAN_sendframe(&frame);
      break;
    case cm_Status3:
      frame.broadcast = CMD_STATUS3.broadcast;
      frame.length = CMD_STATUS3.length;
      frame.data = (uint8_t *)&CMD_STATUS3.data[0];
      r = AVCLAN_sendframe(&frame);
      break;
    case cm_Status4:
      frame.broadcast = CMD_STATUS4.broadcast;
      frame.length = CMD_STATUS4.length;
      frame.data = (uint8_t *)&CMD_STATUS4.data[0];
      r = AVCLAN_sendframe(&frame);
      break;
    case cm_Register:
      frame.broadcast = CMD_REGISTER.broadcast;
      frame.length = CMD_REGISTER.length;
      frame.data = (uint8_t *)&CMD_REGISTER.data[0];
      r = AVCLAN_sendframe(&frame);
      break;
    case cm_Init: // RS232_Print("INIT\n");
      r = AVCLan_SendInitCommands();
      break;
    case cm_Check:
      frame.broadcast = CMD_CHECK.broadcast;
      frame.length = CMD_CHECK.length;
      frame.data = &CMD_CHECK.data[0];
      r = AVCLAN_sendframe(&frame);
      check_timeout = 0;
      CMD_CHECK.data[6]++;
      RS232_Print("AVCCHK\n");
      break;
    case cm_PlayReq1:
      playMode = 0;
      frame.broadcast = CMD_PLAY_OK1.broadcast;
      frame.length = CMD_PLAY_OK1.length;
      frame.data = (uint8_t *)&CMD_PLAY_OK1.data[0];
      r = AVCLAN_sendframe(&frame);
      break;
    case cm_PlayReq2:
    case cm_PlayReq3:
      playMode = 0;
      frame.broadcast = CMD_PLAY_OK2.broadcast;
      frame.length = CMD_PLAY_OK2.length;
      frame.data = (uint8_t *)&CMD_PLAY_OK2.data[0];
      r = AVCLAN_sendframe(&frame);
      if (!r) {
        frame.broadcast = CMD_PLAY_OK3.broadcast;
        frame.length = CMD_PLAY_OK3.length;
        frame.data = (uint8_t *)&CMD_PLAY_OK3.data[0];
        r = AVCLAN_sendframe(&frame);
      }
      CD_Mode = stPlay;
      break;
    case cm_PlayIt:
      playMode = 1;
      RS232_Print("PLAY\n");
      frame.broadcast = CMD_PLAY_OK4.broadcast;
      frame.length = CMD_PLAY_OK4.length;
      frame.data = (uint8_t *)&CMD_PLAY_OK4.data[0];
      CMD_PLAY_OK4.data[7] = cd_Disc;
      CMD_PLAY_OK4.data[8] = cd_Track;
      CMD_PLAY_OK4.data[9] = cd_Time_Min;
      CMD_PLAY_OK4.data[10] = cd_Time_Sec;
      r = AVCLAN_sendframe(&frame);

      if (!r)
        AVCLan_Send_Status();
      CD_Mode = stPlay;
      break;
    case cm_StopReq:
    case cm_StopReq2:
      CD_Mode = stStop;
      playMode = 0;
      frame.broadcast = CMD_STOP1.broadcast;
      frame.length = CMD_STOP1.length;
      frame.data = (uint8_t *)&CMD_STOP1.data[0];
      r = AVCLAN_sendframe(&frame);

      CMD_STOP2.data[7] = cd_Disc;
      CMD_STOP2.data[8] = cd_Track;
      CMD_STOP2.data[9] = cd_Time_Min;
      CMD_STOP2.data[10] = cd_Time_Sec;
      frame.broadcast = CMD_STOP2.broadcast;
      frame.length = CMD_STOP2.length;
      frame.data = (uint8_t *)&CMD_STOP2.data[0];
      r = AVCLAN_sendframe(&frame);
      break;
    case cm_Beep:
      frame.broadcast = CMD_BEEP.broadcast;
      frame.length = CMD_BEEP.length;
      frame.data = (uint8_t *)&CMD_BEEP.data[0];
      r = AVCLAN_sendframe(&frame);
      break;
  }

  answerReq = cm_Null;
  return r;
}

void AVCLan_Register() {
  AVCLAN_frame_t register_frame = {.broadcast = CMD_REGISTER.broadcast,
                                   .sender_addr = CD_ID,
                                   .responder_addr = HU_ID,
                                   .control = 0xF,
                                   .length = CMD_REGISTER.length,
                                   .data = (uint8_t *)&CMD_REGISTER.data[0]};
  RS232_Print("REG_ST\n");
  AVCLAN_sendframe(&register_frame);
  RS232_Print("REG_END\n");
  // AVCLan_Command( cm_Register );
  answerReq = cm_Init;
  AVCLan_SendAnswer();
}

/* Increment packed 2-digit BCD number.
   WARNING: Overflow behavior is incorrect (e.g. `incBCD(0x99) != 0x00`) */
uint8_t incBCD(uint8_t data) {
  if ((data & 0x9) == 0x9)
    return (data + 7);

  return (data + 1);
}

void AVCLAN_printframe(const AVCLAN_frame_t *frame) {
  if (frame->responder_addr == CD_ID ||
      (frame->broadcast && frame->responder_addr == 0x1FF))
    RS232_Print(" < ");
  else
    RS232_Print(">< ");

  RS232_PrintHex4(frame->broadcast);

  RS232_Print(" 0x");
  RS232_PrintHex4(*(((uint8_t *)&frame->sender_addr) + 1));
  RS232_PrintHex8(*(((uint8_t *)&frame->sender_addr) + 0));
  RS232_Print(" 0x");
  RS232_PrintHex4(*(((uint8_t *)&frame->responder_addr) + 1));
  RS232_PrintHex8(*(((uint8_t *)&frame->responder_addr) + 0));

  RS232_Print(" 0x");
  RS232_PrintHex4(frame->control);

  RS232_Print(" 0x");
  RS232_PrintHex4(frame->length);

  for (uint8_t i = 0; i < frame->length; i++) {
    RS232_Print(" 0x");
    RS232_PrintHex8(frame->data[i]);
  }
  RS232_Print("\n");
}

#ifdef SOFTWARE_DEBUG
uint16_t temp_b[100];

void AVCLan_Measure() {
  STOPEvent;

  // uint16_t tmp, tmp1, tmp2, bit0, bit1;
  uint8_t n = 0;

  cbi(TCCR1B, CS12);
  TCCR1B = _BV(CS10);
  TCNT1 = 0;

  char str[5];

  while (n < 100) {
    temp_b[n] = TCNT1;
    while (INPUT_IS_CLEAR) {}
    temp_b[n + 1] = TCNT1;
    while (INPUT_IS_SET) {}
    temp_b[n + 2] = TCNT1;
    while (INPUT_IS_CLEAR) {}
    temp_b[n + 3] = TCNT1;
    while (INPUT_IS_SET) {}
    temp_b[n + 4] = TCNT1;
    while (INPUT_IS_CLEAR) {}
    temp_b[n + 5] = TCNT1;
    while (INPUT_IS_SET) {}
    temp_b[n + 6] = TCNT1;
    while (INPUT_IS_CLEAR) {}
    temp_b[n + 7] = TCNT1;
    while (INPUT_IS_SET) {}
    temp_b[n + 8] = TCNT1;
    while (INPUT_IS_CLEAR) {}
    temp_b[n + 9] = TCNT1;
    while (INPUT_IS_SET) {}
    //
    // while (INPUT_IS_CLEAR) {}
    //
    // tmp1 = TCNT1;
    //
    // while (INPUT_IS_SET) {}
    //
    // tmp2 = TCNT1;
    //
    // bit0 = tmp1-tmp;
    // bit1 = tmp2-tmp1;
    //
    // RS232_Print("1,");
    // RS232_PrintDec(bit1);
    // RS232_Print("\n");
    //
    // RS232_Print("0,");
    // RS232_PrintDec(bit0);
    // RS232_Print("\n");
    n += 10;
  }

  for (uint8_t i = 0; i < 100; i++) {
    itoa(temp_b[i], str);
    if (i & 1) {
      RS232_Print("High,");
    } else {
      RS232_Print("Low,");
    }
    RS232_Print(str);
    RS232_Print("\n");
  }
  RS232_Print("\nDone.\n");

  cbi(TCCR1B, CS10);
  TCCR1B = _BV(CS12);

  STARTEvent;
}
#endif

#ifdef HARDWARE_DEBUG
void SetHighLow() {
  AVC_OUT_EN();
  sbi(TCCR1B, CS10);
  uint16_t n = 60000;
  TCNT1 = 0;
  AVC_SET_LOGICAL_1();
  while (TCNT1 < n) {}
  TCNT1 = 0;
  AVC_SET_LOGICAL_0();
  while (TCNT1 < n) {}
  cbi(TCCR1B, CS10);
  AVC_OUT_DIS();
}
#endif
