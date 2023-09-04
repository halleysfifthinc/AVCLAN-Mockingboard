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

--------------------------------------------------------------------------------------

                                AVC LAN Theory

  The AVC LAN bus is an implementation of the IEBus (mode 1) which is a
  differential signal.

  - Logical `1`: Potential difference between bus lines (BUS+ pin and BUS– pin)
    is 20 mV or lower (floating).
  - Logical `0`: Potential difference between bus lines (BUS+ pin and BUS– pin)
    is 120 mV or higher (driving).

  A nominal bit length is 39 us, composed of 3 periods: preparation,
  synchronization, data.

                                Figure 1. AVCLAN Bus bit format

                             │ Prep │<─ Sync ─>│<─ Data ─>│ ...
    Driving (logical `0`)           ╭──────────╮──────────╮
                                    │          │          │
    Floating (logical `1`) ─────────╯          ╰──────────╰─────────
                             │ 7 μs │── 20 μs ─│─ 12 μs ──│

  The logical value during the data period signifies the bit value, e.g. a bit
  `0` continues the logical `0` (high potential difference between bus lines) of
  the sync period thru the data period, and a bit `1` has a logical `1`
  (low/floating potential between bus lines) during the data period.

        AVC LAN Frame Format
    │ Bits │ Description
    ────────────────────────────────────────
    |  1   │ Start bit
    |  1   │ Direct/broadcast
    |  12  │ Controller address
    |  1   │ Parity
    |  12  │ Peripheral address
    |  1   │ Parity
    |  1   │ *Acknowledge* (read below)
    |  4   │ Control
    |  1   │ Parity
    |  1   │ *Acknowledge*
    |  8   │ Message length (n)
    |  1   │ Parity
    |  1   │ *Acknowledge*
    ────────
       | 8 │ Data
       | 1 │ Parity
       | 1 │ *Acknowledge*
       *repeat `n` times*


  A start bit is nominally 166 us high followed by 19 us low.

  A bit `0` is dominant on the bus, which is a design choice that affects
  bit/interpretation:
    - Low addresses have priority upon transmission conflicts
    - The broadcast bit is `1` for normal communication
    - For acknowledge bits, the receiver extends the logical '0' of the sync
      period to the length of a normal bit `0`. Hence, a NAK (bit `1`) is
      equivalent to no response.

  No acknowledge bits are sent for broadcast frames.

--------------------------------------------------------------------------------------
*/

#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/sfr_defs.h>
#include <stdint.h>

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

uint8_t printAllFrames;

uint8_t playMode;

uint8_t cd_Track;
uint8_t cd_Time_Min;
uint8_t cd_Time_Sec;

uint8_t answerReq;

cd_modes CD_Mode;

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
    {0x63, 0x31, 0xF1, 0x01, 0x28, 0x00, 0x00, 0x01, 0x00, 0x00, 0x80}};

const AVCLAN_KnownMessage_t CMD_STOP1 = {
    UNICAST, 5, {0x00, 0x63, SW_ID, 0x53, 0x01}};
AVCLAN_KnownMessage_t CMD_STOP2 = {
    BROADCAST,
    11,
    {0x63, 0x31, 0xF1, 0x00, 0x30, 0x00, 0x00, 0x01, 0x00, 0x00, 0x80}};

const AVCLAN_KnownMessage_t CMD_BEEP = {
    UNICAST, 5, {0x00, 0x63, 0x29, 0x60, 0x02}};

void AVCLAN_init() {
  // Pull-ups are disabled by default
  // Set pin 6 and 7 as input
  PORTA.DIRCLR = (PIN6_bm | PIN7_bm);
  PORTA.PIN6CTRL = PORT_ISC_INPUT_DISABLE_gc; // Disable input buffer;
  PORTA.PIN7CTRL = PORT_ISC_INPUT_DISABLE_gc; // recommended when using AC

  // Analog comparator config
  AC2.CTRLA = AC_OUTEN_bm | AC_HYSMODE_25mV_gc | AC_ENABLE_bm;

  PORTB.DIRSET = PIN2_bm;                     // Enable AC2 OUT for LED
  PORTB.PIN2CTRL = PORT_ISC_INPUT_DISABLE_gc; // Output only

  // Set AC2 to generate events on async channel 0
  EVSYS.ASYNCCH0 = EVSYS_ASYNCCH0_AC2_OUT_gc;
  EVSYS.ASYNCUSER0 = EVSYS_ASYNCUSER0_ASYNCCH0_gc; // USER0 is TCB0

  // TCB0 for read bit timing
  TCB0.CTRLB = TCB_CNTMODE_PW_gc;
  TCB0.INTCTRL = TCB_CAPT_bm;
  TCB0.EVCTRL = TCB_CAPTEI_bm;
  TCB0.CTRLA = TCB_CLKSEL_CLKDIV2_gc | TCB_ENABLE_bm;

  // TCB1 for send bit timing
  TCB1.CTRLB = TCB_CNTMODE_INT_gc;
  TCB1.CCMP = 0xFFFF;
  TCB1.CTRLA = TCB_CLKSEL_CLKDIV2_gc | TCB_ENABLE_bm;

  answerReq = cm_Null;

  cd_Track = 1;
  cd_Time_Min = 0;
  cd_Time_Sec = 0;
  playMode = 0;
  CD_Mode = stStop;
}

void set_AVC_logic_for(uint8_t val, uint16_t period) {
  TCB1.CNT = 0;
  if (val) {
    AVC_SET_LOGICAL_1();
  } else {
    AVC_SET_LOGICAL_0();
  }
  while (TCB1.CNT <= period) {};

  return;
}

uint8_t AVCLAN_sendbit_start() {
  set_AVC_logic_for(1, 1328); // 166 us @ 125 ns tick (for F_CPU = 16MHz)
  set_AVC_logic_for(0, 152);  // 19 us @ 125 ns tick (for F_CPU = 16MHz)

  return 1;
}

void AVCLAN_sendbit_1() {
  set_AVC_logic_for(1, 164); // 20.5 us @ 125 ns tick (for F_CPU = 16MHz)
  set_AVC_logic_for(0, 152); // 19 us @ 125 ns tick (for F_CPU = 16MHz)
}

void AVCLAN_sendbit_0() {
  set_AVC_logic_for(1, 272); // 34 us @ 125 ns tick (for F_CPU = 16MHz)
  set_AVC_logic_for(0, 44);  // 5.5 us @ 125 ns tick (for F_CPU = 16MHz)
}

void AVCLAN_sendbit_ACK() {
  TCB1.CNT = 0;
  while (INPUT_IS_CLEAR) {
    if (TCB1.CNT >= 900)
      return; // max wait time
  }

  AVC_OUT_EN();

  set_AVC_logic_for(1, 272); // 34 us @ 125 ns tick (for F_CPU = 16MHz)
  set_AVC_logic_for(0, 44);  // 5.5 us @ 125 ns tick (for F_CPU = 16MHz)

  AVC_OUT_DIS();
}

void AVCLAN_sendbit_parity(uint8_t parity) {
  if (parity) {
    AVCLAN_sendbit_1();
  } else {
    AVCLAN_sendbit_0();
  }
}

#define AVCLAN_sendbits(bits, len)                                             \
  _Generic((bits),                                                             \
      const uint16_t *: AVCLAN_sendbitsl,                                      \
      uint16_t *: AVCLAN_sendbitsl,                                            \
      const uint8_t *: AVCLAN_sendbitsi,                                       \
      uint8_t *: AVCLAN_sendbitsi)(bits, len)

// Send `len` bits on the AVCLAN bus; returns the even parity
uint8_t AVCLAN_sendbitsi(const uint8_t *bits, int8_t len) {
  uint8_t b = *bits;
  uint8_t parity = 0;
  int8_t len_mod8 = 8;

  if (len & 0x7) {
    len_mod8 = (int8_t)(len & 0x7);
    b <<= (uint8_t)(8 - len_mod8);
  }

  while (len > 0) {
    len -= len_mod8;
    for (; len_mod8 > 0; len_mod8--) {
      if (b & 0x80) {
        AVCLAN_sendbit_1();
        parity++;
      } else {
        AVCLAN_sendbit_0();
      }
      b <<= 1;
    }
    len_mod8 = 8;
    b = *--bits;
  }
  return (parity & 1);
}

// Send `len` bits on the AVCLAN bus; returns the even parity
uint8_t AVCLAN_sendbitsl(const uint16_t *bits, int8_t len) {
  return AVCLAN_sendbitsi((const uint8_t *)bits + 1, len);
}

uint8_t AVCLAN_sendbyte(const uint8_t *byte) {
  uint8_t b = *byte;
  uint8_t parity = 0;

  for (uint8_t nbits = 8; nbits > 0; nbits--) {
    if (b & 0x80) {
      AVCLAN_sendbit_1();
      parity++;
    } else {
      AVCLAN_sendbit_0();
    }
    b <<= 1;
  }
  return (parity & 1);
}

#define READING_BYTE GPIOR1
#define READING_NBITS GPIOR2
#define READING_PARITY GPIOR3

ISR(TCB0_INT_vect) {
  // If input was set for less than 26 us (a generous half period), bit was a 1
  if (TCB0.CCMP < 208) {
    READING_BYTE++;
    READING_PARITY++;
  }
  READING_BYTE <<= 1;
  READING_NBITS--;
}

#define AVCLAN_readbits(bits, len)                                             \
  _Generic((bits),                                                             \
      const uint16_t *: AVCLAN_readbitsl,                                      \
      uint16_t *: AVCLAN_readbitsl,                                            \
      const uint8_t *: AVCLAN_readbitsi,                                       \
      uint8_t *: AVCLAN_readbitsi)(bits, len)

// Send `len` bits on the AVCLAN bus; returns the even parity
uint8_t AVCLAN_readbitsi(uint8_t *bits, uint8_t len) {
  cli();
  READING_BYTE = 0;
  READING_PARITY = 0;
  READING_NBITS = len;
  sei();

  while (READING_NBITS != 0) {};

  cli();
  *bits = READING_BYTE;
  uint8_t parity = READING_PARITY;
  sei();

  return (parity & 1);
}

// Send `len` bits on the AVCLAN bus; returns the even parity
uint8_t AVCLAN_readbitsl(uint16_t *bits, int8_t len) {
  uint8_t parity = 0;
  if (len > 8) {
    uint8_t over = len - 8;
    parity = AVCLAN_readbitsi((uint8_t *)bits + 0, over);
    len -= over;
  }
  parity += AVCLAN_readbitsi((uint8_t *)bits + 1, len);

  return (parity & 1);
}

// Read a byte on the AVCLAN bus
uint8_t AVCLAN_readbyte(uint8_t *byte) {
  cli();
  READING_BYTE = 0;
  READING_NBITS = 8;
  sei();

  while (READING_NBITS != 0) {};

  cli();
  *byte = READING_BYTE;
  uint8_t parity = READING_PARITY;
  sei();

  return (parity & 1);
}

uint8_t AVCLAN_readbit_ACK() {
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
  uint8_t tmp = 0;
  AVCLAN_readbits(&tmp, 1); // Start bit

  AVCLAN_readbits((uint8_t *)&frame.broadcast, 1);

  parity = AVCLAN_readbits(&frame.controller_addr, 12);
  AVCLAN_readbits(&tmp, 1);
  if (parity != tmp) {
    STARTEvent;
    return 0;
  }

  parity = AVCLAN_readbits(&frame.peripheral_addr, 12);
  AVCLAN_readbits(&tmp, 1);
  if (parity != tmp) {
    STARTEvent;
    return 0;
  }

  // is this command for me ?
  for_me = (frame.peripheral_addr == CD_ID);

  if (for_me)
    AVCLAN_sendbit_ACK();
  else
    AVCLAN_readbits(&tmp, 1);

  parity = AVCLAN_readbits(&frame.control, 4);
  AVCLAN_readbits(&tmp, 1);
  if (parity != tmp) {
    STARTEvent;
    return 0;
  } else if (for_me) {
    AVCLAN_sendbit_ACK();
  } else {
    AVCLAN_readbits(&tmp, 1);
  }

  parity = AVCLAN_readbyte(&frame.length);
  AVCLAN_readbits(&tmp, 1);
  if (parity != tmp) {
    STARTEvent;
    return 0;
  } else if (for_me) {
    AVCLAN_sendbit_ACK();
  } else {
    AVCLAN_readbits(&tmp, 1);
  }

  if (frame.length > MAXMSGLEN) {
    //	RS232_Print("LAN> Command error");
    STARTEvent;
    return 0;
  }

  for (i = 0; i < frame.length; i++) {
    parity = AVCLAN_readbyte(&frame.data[i]);
    AVCLAN_readbits(&tmp, 1);
    if (parity != tmp) {
      STARTEvent;
      return 0;
    } else if (for_me) {
      AVCLAN_sendbit_ACK();
    } else {
      AVCLAN_readbits(&tmp, 1);
    }
  }

  STARTEvent;

  if (printAllFrames)
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

  AVCLAN_sendbit_start();
  AVCLAN_sendbits((uint8_t *)&frame->broadcast, 1);

  parity = AVCLAN_sendbits(&frame->controller_addr, 12);
  AVCLAN_sendbit_parity(parity);

  parity = AVCLAN_sendbits(&frame->peripheral_addr, 12);
  AVCLAN_sendbit_parity(parity);

  if (!frame->broadcast && AVCLAN_readbit_ACK()) {
    AVC_OUT_DIS();
    STARTEvent;
    RS232_Print("Error NAK: Addresses\n");
    return 1;
  }

  parity = AVCLAN_sendbits(&frame->control, 4);
  AVCLAN_sendbit_parity(parity);

  if (!frame->broadcast && AVCLAN_readbit_ACK()) {
    AVC_OUT_DIS();
    STARTEvent;
    RS232_Print("Error NAK: Control\n");
    return 2;
  }

  parity = AVCLAN_sendbyte(&frame->length); // data length
  AVCLAN_sendbit_parity(parity);

  if (!frame->broadcast && AVCLAN_readbit_ACK()) {
    AVC_OUT_DIS();
    STARTEvent;
    RS232_Print("Error NAK: Message length\n");
    return 3;
  }

  for (uint8_t i = 0; i < frame->length; i++) {
    parity = AVCLAN_sendbyte(&frame->data[i]);
    AVCLAN_sendbit_parity(parity);
    // Based on the µPD6708 datasheet, ACK bit for broadcast doesn't seem
    // necessary (i.e. This deviates from the previous broadcast specific
    // function that sent an extra `1` bit after each byte/parity)
    if (!frame->broadcast && AVCLAN_readbit_ACK()) {
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

  if (printAllFrames)
    AVCLAN_printframe(frame);

  return 0;
}

uint8_t AVCLan_SendInitCommands() {
  uint8_t r;
  AVCLAN_frame_t frame = {.broadcast = BROADCAST,
                          .controller_addr = CD_ID,
                          .peripheral_addr = HU_ID,
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
  STATUS[6] = cd_Track;
  STATUS[7] = cd_Time_Min;
  STATUS[8] = cd_Time_Sec;
  STATUS[9] = 0;

  AVCLAN_frame_t status = {.broadcast = UNICAST,
                           .controller_addr = CD_ID,
                           .peripheral_addr = HU_ID,
                           .control = 0xF,
                           .length = 11,
                           .data = &STATUS[0]};

  AVCLAN_sendframe(&status);
}

uint8_t AVCLan_SendAnswer() {
  uint8_t r = 0;
  AVCLAN_frame_t frame = {.broadcast = UNICAST,
                          .controller_addr = CD_ID,
                          .peripheral_addr = HU_ID,
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
                                   .controller_addr = CD_ID,
                                   .peripheral_addr = HU_ID,
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

void AVCLAN_printframe(const AVCLAN_frame_t *frame) {
  if (frame->peripheral_addr == CD_ID ||
      (frame->broadcast && frame->peripheral_addr == 0x1FF))
    RS232_Print(" < ");
  else
    RS232_Print(">< ");

  RS232_PrintHex4(frame->broadcast);

  RS232_Print(" 0x");
  RS232_PrintHex4(*(((uint8_t *)&frame->controller_addr) + 1));
  RS232_PrintHex8(*(((uint8_t *)&frame->controller_addr) + 0));
  RS232_Print(" 0x");
  RS232_PrintHex4(*(((uint8_t *)&frame->peripheral_addr) + 1));
  RS232_PrintHex8(*(((uint8_t *)&frame->peripheral_addr) + 0));

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
