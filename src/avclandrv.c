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
  differential signal; IEBus is electrically (but not logically) compatible with
  CAN bus.

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
                             │ 6 μs │── 19 μs ─│─ 13 μs ──│

  The logical value during the data period signifies the bit value, e.g. a bit
  `0` continues the logical `0` (high potential difference between bus lines) of
  the sync period thru the data period, and a bit `1` has a logical `1`
  (low/floating potential between bus lines) during the data period. Using the
  TCB pulse-width and frequency measure mode, the total bit length differs for
  bit `1` and `0`; detailed bit timing can be found in "timing.h". The bus
  idles at low potential (floating).

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


  A start bit is nominally 169 us high followed by 20 us low.

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
#include <stdlib.h>

#include "avclandrv.h"
#include "com232.h"

// F_CPU defined in timing.h and potentially needed by avr-libc (e.g. delay.h)
#include "timing.h"

// clang-format off
#define AVC_SET_LOGICAL_1()                                                    \
  __asm__ __volatile__(                                                        \
      "cbi %[vporta_out], 4; \n\t"                                             \
      "sbi %[vportc_out], 0; \n\t"                                             \
      ::[vporta_out] "I"(_SFR_IO_ADDR(VPORTA_OUT)),                            \
        [vportc_out] "I"(_SFR_IO_ADDR(VPORTC_OUT)));
#define AVC_SET_LOGICAL_0()                                                    \
  __asm__ __volatile__(                                                        \
      "sbi %[vporta_out], 4; \n\t"                                             \
      "cbi %[vportc_out], 0; \n\t"                                             \
      ::[vporta_out] "I"(_SFR_IO_ADDR(VPORTA_OUT)),                            \
        [vportc_out] "I"(_SFR_IO_ADDR(VPORTC_OUT)));
// clang-format on

// Name difference between avr-libc and Microchip pack
#if defined(EVSYS_ASYNCCH00_bm)
  #define EVSYS_ASYNCCH0_0_bm EVSYS_ASYNCCH00_bm
#endif

#define READING_BYTE   GPIOR1
#define READING_NBITS  GPIOR2
#define READING_PARITY GPIOR3

#ifdef SOFTWARE_DEBUG
  #define TCB_CNTMODE TCB_CNTMODE_FRQPW_gc
#else
  #define TCB_CNTMODE TCB_CNTMODE_PW_gc
#endif


uint8_t printAllFrames;
uint8_t verbose;
uint8_t printBinary;

AVCLAN_CD_Status_t cd_status;

uint8_t *cd_Track;
uint8_t *cd_Time_Min;
uint8_t *cd_Time_Sec;

uint8_t answerReq;

cd_modes CD_Mode;

#ifdef SOFTWARE_DEBUG
uint8_t pulse_count = 0;
uint16_t period = 0;
#endif

uint16_t pulsewidth;

#define SW_ID 0x11 // 11 For my stereo

// commands
const uint8_t stat1[] = {0x00, 0x00, 0x01, 0x0A};
const uint8_t stat2[] = {0x00, 0x00, 0x01, 0x08};
const uint8_t stat3[] = {0x00, 0x00, 0x01, 0x0D};
const uint8_t stat4[] = {0x00, 0x00, 0x01, 0x0C};

// broadcast
const uint8_t lan_stat1[] = {0x00, 0x01, 0x0A};
const uint8_t lan_reg[] = {SW_ID, 0x01, 0x00};
const uint8_t lan_init[] = {SW_ID, 0x01, 0x01};
const uint8_t lan_check[] = {SW_ID, 0x01, 0x20};
const uint8_t lan_playit[] = {SW_ID, 0x01, 0x45, 0x63};

const uint8_t play_req1[] = {0x00, 0x25, 0x63, 0x80};

#ifdef __AVENSIS__
const uint8_t play_req2[] = {0x00, SW_ID, 0x63, 0x42};
#else
const uint8_t play_req2[] = {0x00, SW_ID, 0x63, 0x42, 0x01, 0x00};
#endif

const uint8_t play_req3[] = {0x00, SW_ID, 0x63, 0x42, 0x41};
const uint8_t stop_req[] = {0x00, SW_ID, 0x63, 0x43, 0x01};
const uint8_t stop_req2[] = {0x00, SW_ID, 0x63, 0x43, 0x41};

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

uint8_t CheckCmd(const AVCLAN_frame_t *frame, const uint8_t *cmd, uint8_t l);

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
  TCB0.CTRLB = TCB_CNTMODE;
  TCB0.INTCTRL = TCB_CAPT_bm;
  TCB0.EVCTRL = TCB_CAPTEI_bm;
  TCB0.CTRLA = TCB_CLKSEL | TCB_ENABLE_bm;

  // TCB1 for send bit timing
  TCB1.CTRLB = TCB_CNTMODE_INT_gc;
  TCB1.CCMP = 0xFFFF;
  TCB1.CTRLA = TCB_CLKSEL | TCB_ENABLE_bm;

  // Setup RTC as 1 sec periodic timer
  loop_until_bit_is_clear(RTC_STATUS, RTC_CTRLABUSY_bp);
  RTC.CTRLA = RTC_PRESCALER_DIV1_gc;
  RTC.CLKSEL = RTC_CLKSEL_INT32K_gc;
  RTC.PITINTCTRL = RTC_PI_bm;
  loop_until_bit_is_clear(RTC_PITSTATUS, RTC_CTRLBUSY_bp);
  RTC.PITCTRLA = RTC_PERIOD_CYC32768_gc | RTC_PITEN_bm;

  // Set bus output pins to idle
  AVC_SET_LOGICAL_1();

  AVCLAN_muteDevice(0); // unmute AVCLAN bus TX

  answerReq = cm_Null;

  cd_status.cd1 = 1;
  cd_status.disc = 1;
  cd_status.cd2 = cd_status.cd3 = cd_status.cd4 = cd_status.cd5 =
      cd_status.cd6 = 0;
  cd_status.state = cd_LOADING;
  cd_status.disk_random = 0;
  cd_status.random = 0;
  cd_status.disk_repeat = 0;
  cd_status.repeat = 0;
  cd_status.scan = 0;

  cd_status.track = 1;
  cd_status.mins = 0;
  cd_status.secs = 0;

  cd_Track = &cd_status.track;
  cd_Time_Min = &cd_status.mins;
  cd_Time_Sec = &cd_status.secs;

  CD_Mode = stStop;
}

/* Increment packed 2-digit BCD number.
   WARNING: Overflow behavior is incorrect (e.g. `incBCD(0x99) != 0x00`) */
uint8_t incBCD(uint8_t data) {
  if ((data & 0x9) == 0x9)
    return (data + 7);

  return (data + 1);
}

// Periodic interrupt with a 1 sec period
ISR(RTC_PIT_vect) {
  if (CD_Mode == stPlay) {
    uint8_t sec = *cd_Time_Sec;
    uint8_t min = *cd_Time_Min;
    sec = incBCD(sec);
    if (sec == 0x60) {
      *cd_Time_Sec = 0;
      min = incBCD(min);
      if (min == 0xA0) {
        *cd_Time_Min = 0x0;
      }
    }
    answerReq = cm_CDStatus;
  }
  RTC.PITINTFLAGS |= RTC_PI_bm;
}

// Mute device TX on AVCLAN bus
void AVCLAN_muteDevice(uint8_t mute) {
  if (mute) {
    // clang-format off
    __asm__ __volatile__("cbi %[vporta_dir], 4; \n\t"
                         "cbi %[vportc_dir], 0; \n\t"
                         ::
                         [vporta_dir] "I"(_SFR_IO_ADDR(VPORTA_DIR)),
                         [vportc_dir] "I"(_SFR_IO_ADDR(VPORTC_DIR)));
    // clang-format on
  } else {
    // clang-format off
    __asm__ __volatile__("sbi %[vporta_dir], 4; \n\t"
                         "sbi %[vportc_dir], 0; \n\t"
                         ::
                         [vporta_dir] "I"(_SFR_IO_ADDR(VPORTA_DIR)),
                         [vportc_dir] "I"(_SFR_IO_ADDR(VPORTC_DIR)));
    // clang-format on
  }
}

// Returns true if device TX is muted on AVCLAN bus
static inline uint8_t AVCLAN_ismuted() {
  return (((VPORTA_DIR & PIN4_bm) | (VPORTA_DIR & PIN0_bm)) == 0);
}

// Set AVC bus to `val` (logical 1 or 0) for `period` ticks of TCB1
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

void AVCLAN_sendbit_start() {
  set_AVC_logic_for(0, AVCLAN_STARTBIT_LOGIC_0);
  set_AVC_logic_for(1, AVCLAN_STARTBIT_LOGIC_1);
}

static inline void AVCLAN_sendbit_1() {
  set_AVC_logic_for(0, AVCLAN_BIT1_LOGIC_0);
  set_AVC_logic_for(1, AVCLAN_BIT1_LOGIC_1);
}

static inline void AVCLAN_sendbit_0() {
  set_AVC_logic_for(0, AVCLAN_BIT0_LOGIC_0);
  set_AVC_logic_for(1, AVCLAN_BIT0_LOGIC_1);
}

void AVCLAN_sendbit_ACK() {
  TCB1.CNT = 0;

  // Wait for controller to begin ACK bit
  while (BUS_IS_IDLE) {
    // Wait for approx the length of a bit; any longer and something has clearly
    // gone wrong
    if (TCB1.CNT >= AVCLAN_BIT_LENGTH_MAX)
      return;
  }

  set_AVC_logic_for(0, AVCLAN_BIT0_LOGIC_0);
  set_AVC_logic_for(1, AVCLAN_BIT0_LOGIC_1);
}

// Returns true if an ACK bit was sent by the peripheral
uint8_t AVCLAN_readbit_ACK() {
  TCB1.CNT = 0;
  set_AVC_logic_for(0, AVCLAN_BIT1_LOGIC_0);
  AVC_SET_LOGICAL_1(); // Stop driving bus

  while (1) {
    if (!BUS_IS_IDLE && (TCB1.CNT > AVCLAN_READBIT_THRESHOLD))
      break; // ACK
    if (TCB1.CNT > AVCLAN_BIT_LENGTH_MAX)
      return 0; // NAK
  }

  // Check/wait in case we get here before peripheral finishes ACK bit
  while (!BUS_IS_IDLE) {}
  return 1;
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

ISR(TCB0_INT_vect) {
#ifdef SOFTWARE_DEBUG
  pulse_count++;
  period = TCB0.CNT;
#endif

  READING_BYTE <<= 1;
  // If the logical `0` pulse was less than the sync + data period threshold,
  // bit was a 1
  pulsewidth = TCB0.CCMP;
  if (pulsewidth < (uint16_t)AVCLAN_READBIT_THRESHOLD) {
    READING_BYTE++;
    READING_PARITY++;
  }
  READING_NBITS--;
}

#define AVCLAN_readbits(bits, len)                                             \
  _Generic((bits),                                                             \
      const uint16_t *: AVCLAN_readbitsl,                                      \
      uint16_t *: AVCLAN_readbitsl,                                            \
      const uint8_t *: AVCLAN_readbitsi,                                       \
      uint8_t *: AVCLAN_readbitsi)(bits, len)

// Read `len` bits on the AVCLAN bus; returns the even parity
uint8_t AVCLAN_readbitsi(uint8_t *bits, uint8_t len) {
  cli();
  READING_BYTE = 0;
  READING_PARITY = 0;
  READING_NBITS = len;
  sei();

  TCB1.CNT = 0;
  while (READING_NBITS != 0) {
    // 200% the duration of `len` bits
    if (TCB1.CNT > ((uint16_t)AVCLAN_BIT_LENGTH_MAX * 2 * len)) {
      READING_BYTE = 0;
      READING_PARITY = 0;
      break; // Should have finished by now; something's wrong
    }
  };

  cli();
  *bits = READING_BYTE;
  uint8_t parity = READING_PARITY;
  sei();

  return (parity & 1);
}

// Read `len` bits on the AVCLAN bus; returns the even parity
uint8_t AVCLAN_readbitsl(uint16_t *bits, int8_t len) {
  uint8_t parity = 0;
  if (len > 8) {
    uint8_t over = len - 8;
    parity = AVCLAN_readbitsi((uint8_t *)bits + 1, over);
    len -= over;
  }
  parity += AVCLAN_readbitsi((uint8_t *)bits + 0, len);

  return (parity & 1);
}

// Read a byte on the AVCLAN bus
uint8_t AVCLAN_readbyte(uint8_t *byte) {
  cli();
  READING_BYTE = 0;
  READING_PARITY = 0;
  READING_NBITS = 8;
  sei();

  TCB1.CNT = 0;
  while (READING_NBITS != 0) {
    // 200% the length of a byte
    if (TCB1.CNT > ((uint16_t)AVCLAN_BIT_LENGTH_MAX * 2 * 8)) {
      READING_BYTE = 0;
      READING_PARITY = 0;
      break; // Should have finished by now; something's wrong
    }
  };

  cli();
  *byte = READING_BYTE;
  uint8_t parity = READING_PARITY;
  sei();

  return (parity & 1);
}

uint8_t AVCLAN_readframe() {
  STOPEvent; // disable timer1 interrupt

  uint8_t data[MAXMSGLEN];
  AVCLAN_frame_t frame = {
      .data = data,
  };

  uint8_t parity = 0;
  uint8_t tmp = 0;

  TCB1.CNT = 0;
  while (!BUS_IS_IDLE) {
    if (TCB1.CNT > (uint16_t)AVCLAN_STARTBIT_LOGIC_0 * 1.2) {
      STARTEvent;
      return 0;
    }
  }
  uint16_t startbitlen = TCB1.CNT;
  if (startbitlen < (uint16_t)(AVCLAN_STARTBIT_LOGIC_0 * 0.8)) {
    RS232_Print("ERR: Short start bit.\n");
    STARTEvent;
    return 0;
  }
  // Otherwise that was a start bit

  AVCLAN_readbits((uint8_t *)&frame.broadcast, 1);

  parity = AVCLAN_readbits(&frame.controller_addr, 12);
  AVCLAN_readbits(&tmp, 1);
  if (parity != (tmp & 1)) {
    RS232_Print("ERR: Bad controller addr. parity");
    if (verbose) {
      RS232_Print("; read 0x");
      RS232_PrintHex12(frame.controller_addr);
      RS232_Print(" and calculated parity=");
      RS232_PrintHex4(parity);
      RS232_Print(" but got ");
      RS232_PrintHex4(tmp & 1);
    }
    RS232_Print(".\n");
    STARTEvent;
    return 0;
  }

  parity = AVCLAN_readbits(&frame.peripheral_addr, 12);
  AVCLAN_readbits(&tmp, 1);
  if (parity != (tmp & 1)) {
    RS232_Print("Bad peripheral addr. parity");
    if (verbose) {
      RS232_Print("; read 0x");
      RS232_PrintHex12(frame.peripheral_addr);
      RS232_Print(" and calculated parity=");
      RS232_PrintHex4(parity);
      RS232_Print(" but got ");
      RS232_PrintHex4(tmp & 1);
    }
    RS232_Print(".\n");
    STARTEvent;
    return 0;
  }

  uint8_t shouldACK =
      !AVCLAN_ismuted() && (frame.peripheral_addr == DEVICE_ADDR);

  if (shouldACK)
    AVCLAN_sendbit_ACK();
  else
    AVCLAN_readbits(&tmp, 1);

  parity = AVCLAN_readbits(&frame.control, 4);
  AVCLAN_readbits(&tmp, 1);
  if (parity != (tmp & 1)) {
    RS232_Print("Bad control parity");
    if (verbose) {
      RS232_Print("; read 0x");
      RS232_PrintHex4(frame.control);
      RS232_Print(" and calculated parity=");
      RS232_PrintHex4(parity);
      RS232_Print(" but got ");
      RS232_PrintHex4(tmp & 1);
    }
    RS232_Print(".\n");
    STARTEvent;
    return 0;
  } else if (shouldACK) {
    AVCLAN_sendbit_ACK();
  } else {
    AVCLAN_readbits(&tmp, 1);
  }

  parity = AVCLAN_readbyte(&frame.length);
  AVCLAN_readbits(&tmp, 1);
  if (parity != (tmp & 1)) {
    RS232_Print("Bad length parity");
    if (verbose) {
      RS232_Print("; read 0x");
      RS232_PrintHex4(frame.length);
      RS232_Print(" and calculated parity=");
      RS232_PrintHex4(parity);
      RS232_Print(" but got ");
      RS232_PrintHex4(tmp & 1);
    }
    RS232_Print(".\n");
    STARTEvent;
    return 0;
  } else if (shouldACK) {
    AVCLAN_sendbit_ACK();
  } else {
    AVCLAN_readbits(&tmp, 1);
  }

  if (frame.length == 0 || frame.length > MAXMSGLEN) {
    RS232_Print("Bad length; got 0x");
    RS232_PrintHex4(frame.length);
    RS232_Print(".\n");
    STARTEvent;
    return 0;
  }

  for (uint8_t i = 0; i < frame.length; i++) {
    parity = AVCLAN_readbyte(&frame.data[i]);
    AVCLAN_readbits(&tmp, 1);
    if (parity != (tmp & 1)) {
      RS232_Print("Bad data parity");
      if (verbose) {
        RS232_Print("; read 0x");
        RS232_PrintHex4(frame.data[i]);
        RS232_Print(" and calculated parity=");
        RS232_PrintHex4(parity);
        RS232_Print(" but got ");
        RS232_PrintHex4(tmp & 1);
      }
      RS232_Print(".\n");
      STARTEvent;
      return 0;
    } else if (shouldACK) {
      AVCLAN_sendbit_ACK();
    } else {
      AVCLAN_readbits(&tmp, 1);
    }
  }

  STARTEvent;

  if (printAllFrames)
    AVCLAN_printframe(&frame, printBinary);

  if (!AVCLAN_ismuted()) {
    if (shouldACK) {
      if (CheckCmd(&frame, stat1, sizeof(stat1))) {
        answerReq = cm_Status1;
        return 1;
      }
      if (CheckCmd(&frame, stat2, sizeof(stat2))) {
        answerReq = cm_Status2;
        return 1;
      }
      if (CheckCmd(&frame, stat3, sizeof(stat3))) {
        answerReq = cm_Status3;
        return 1;
      }
      if (CheckCmd(&frame, stat4, sizeof(stat4))) {
        answerReq = cm_Status4;
        return 1;
      }
      // if (CheckCmd((uint8_t*)stat5)) {
      //   answerReq = cm_Status5;
      //   return 1;
      // }

      if (CheckCmd(&frame, play_req1, sizeof(play_req1))) {
        answerReq = cm_PlayReq1;
        return 1;
      }
      if (CheckCmd(&frame, play_req2, sizeof(play_req2))) {
        answerReq = cm_PlayReq2;
        return 1;
      }
      if (CheckCmd(&frame, play_req3, sizeof(play_req3))) {
        answerReq = cm_PlayReq3;
        return 1;
      }
      if (CheckCmd(&frame, stop_req, sizeof(stop_req))) {
        answerReq = cm_StopReq;
        return 1;
      }
      if (CheckCmd(&frame, stop_req2, sizeof(stop_req2))) {
        answerReq = cm_StopReq2;
        return 1;
      }
    } else { // broadcast check

      if (CheckCmd(&frame, lan_playit, sizeof(lan_playit))) {
        answerReq = cm_PlayIt;
        return 1;
      }
      if (CheckCmd(&frame, lan_check, sizeof(lan_check))) {
        answerReq = cm_Check;
        CMD_CHECK.data[4] = frame.data[3];
        return 1;
      }
      if (CheckCmd(&frame, lan_reg, sizeof(lan_reg))) {
        answerReq = cm_Register;
        return 1;
      }
      if (CheckCmd(&frame, lan_init, sizeof(lan_init))) {
        answerReq = cm_Init;
        return 1;
      }
      if (CheckCmd(&frame, lan_stat1, sizeof(lan_stat1))) {
        answerReq = cm_Status1;
        return 1;
      }
    }
  }

  answerReq = cm_Null;
  return 1;
}

uint8_t AVCLAN_sendframe(const AVCLAN_frame_t *frame) {
  if (AVCLAN_ismuted())
    return 1;

  STOPEvent;

  // wait for free line
  uint8_t line_busy = 1;
  uint8_t parity = 0;

  TCB1.CNT = 0;
  while (BUS_IS_IDLE) {
    // Wait for 120% of a bit length
    if (TCB1.CNT >= (uint16_t)(AVCLAN_BIT_LENGTH_MAX * 2))
      break;
  }

  // End of first loop could be due to bus being driven
  TCB1.CNT = 0;
  if (!BUS_IS_IDLE) {
    // Some other device started sending
    // Can't yet simultaneously send and recieve to do proper CSMA/CD
    return 1;

    // Beginnings of CSMA/CD
    // do {
    //   if (TCB1.CNT >= (uint16_t)(AVCLAN_STARTBIT_LOGIC_0 * 1.2))
    //     return 1; // Something's hinky; nothing is longer than the start bit
    // } while (!BUS_IS_IDLE);
    // if (TCB1.CNT <= (uint16_t)(AVCLAN_STARTBIT_LOGIC_0 * 0.8))
    //   return 1; // Shouldn't be possible (waiting 2 bit lengths with idle
    //   bus,
    //             // then next bit should be a long one ie start)
    // set_AVC_logic_for(1, AVCLAN_STARTBIT_LOGIC_1); // wait for end of start
    // bit
  } else {
    AVCLAN_sendbit_start();
  }
  AVCLAN_sendbits((uint8_t *)&frame->broadcast, 1);

  parity = AVCLAN_sendbits(&frame->controller_addr, 12);
  AVCLAN_sendbit_parity(parity);

  parity = AVCLAN_sendbits(&frame->peripheral_addr, 12);
  AVCLAN_sendbit_parity(parity);

  if (frame->broadcast && !AVCLAN_readbit_ACK()) {
    STARTEvent;
    RS232_Print("Error NAK: Addresses\n");
    return 1;
  }

  parity = AVCLAN_sendbits(&frame->control, 4);
  AVCLAN_sendbit_parity(parity);

  if (frame->broadcast && !AVCLAN_readbit_ACK()) {
    STARTEvent;
    RS232_Print("Error NAK: Control\n");
    return 2;
  }

  parity = AVCLAN_sendbyte(&frame->length); // data length
  AVCLAN_sendbit_parity(parity);

  if (frame->broadcast && !AVCLAN_readbit_ACK()) {
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
    if (frame->broadcast && !AVCLAN_readbit_ACK()) {
      STARTEvent;
      RS232_Print("Error NAK (Data: ");
      RS232_PrintHex8(i);
      RS232_Print(")\n");
      return 4;
    }
    // else
    //   AVCLAN_sendbit_1();
  }

  // back to read mode
  STARTEvent;

  if (printAllFrames)
    AVCLAN_printframe(frame, printBinary);

  return 0;
}

uint8_t AVCLAN_responseNeeded() { return (answerReq != 0); }

uint8_t AVCLAN_respond() {
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
    // case cm_Init: // RS232_Print("INIT\n");
    //   r = AVCLan_SendInitCommands();
    //   break;
    case cm_Check:
      frame.broadcast = CMD_CHECK.broadcast;
      frame.length = CMD_CHECK.length;
      frame.data = CMD_CHECK.data;
      r = AVCLAN_sendframe(&frame);
      CMD_CHECK.data[6]++;
      RS232_Print("AVCCHK\n");
      break;
    case cm_PlayReq1:
      frame.broadcast = CMD_PLAY_OK1.broadcast;
      frame.length = CMD_PLAY_OK1.length;
      frame.data = (uint8_t *)&CMD_PLAY_OK1.data;
      r = AVCLAN_sendframe(&frame);
      break;
    case cm_PlayReq2:
    case cm_PlayReq3:
      frame.broadcast = CMD_PLAY_OK2.broadcast;
      frame.length = CMD_PLAY_OK2.length;
      frame.data = (uint8_t *)&CMD_PLAY_OK2.data;
      r = AVCLAN_sendframe(&frame);
      if (!r) {
        frame.broadcast = CMD_PLAY_OK3.broadcast;
        frame.length = CMD_PLAY_OK3.length;
        frame.data = (uint8_t *)&CMD_PLAY_OK3.data;
        r = AVCLAN_sendframe(&frame);
      }
      CD_Mode = stPlay;
      break;
    case cm_PlayIt:
      RS232_Print("PLAY\n");
      frame.broadcast = CMD_PLAY_OK4.broadcast;
      frame.length = CMD_PLAY_OK4.length;
      frame.data = (uint8_t *)&CMD_PLAY_OK4.data;
      CMD_PLAY_OK4.data[8] = *cd_Track;
      CMD_PLAY_OK4.data[9] = *cd_Time_Min;
      CMD_PLAY_OK4.data[10] = *cd_Time_Sec;
      r = AVCLAN_sendframe(&frame);
      CD_Mode = stPlay;
    case cm_CDStatus:
      if (!r)
        AVCLan_Send_Status();
      break;
    case cm_StopReq:
    case cm_StopReq2:
      CD_Mode = stStop;
      frame.broadcast = CMD_STOP1.broadcast;
      frame.length = CMD_STOP1.length;
      frame.data = (uint8_t *)&CMD_STOP1.data;
      r = AVCLAN_sendframe(&frame);

      CMD_STOP2.data[8] = *cd_Track;
      CMD_STOP2.data[9] = *cd_Time_Min;
      CMD_STOP2.data[10] = *cd_Time_Sec;
      frame.broadcast = CMD_STOP2.broadcast;
      frame.length = CMD_STOP2.length;
      frame.data = (uint8_t *)&CMD_STOP2.data;
      r = AVCLAN_sendframe(&frame);
      break;
    case cm_Beep:
      frame.broadcast = CMD_BEEP.broadcast;
      frame.length = CMD_BEEP.length;
      frame.data = (uint8_t *)&CMD_BEEP.data;
      r = AVCLAN_sendframe(&frame);
      break;
  }

  answerReq = cm_Null;
  return r;
}

void AVCLAN_printframe(const AVCLAN_frame_t *frame, uint8_t binary) {
  if (binary) {
    RS232_SendByte(0x10); // Data Link Escape, signaling binary data forthcoming
    RS232_SendByte(frame->broadcast);

    // Send addresses in big-endian order
    RS232_SendByte(*(((uint8_t *)&frame->controller_addr) + 1));
    RS232_SendByte(*(((uint8_t *)&frame->controller_addr) + 0));
    RS232_SendByte(*(((uint8_t *)&frame->peripheral_addr) + 1));
    RS232_SendByte(*(((uint8_t *)&frame->peripheral_addr) + 0));

    RS232_SendByte(frame->control);
    RS232_SendByte(frame->length);
    RS232_sendbytes(frame->data, frame->length);
    RS232_SendByte(0x17); // End of transmission block
    RS232_Print("\n");
  } else {
    RS232_PrintHex4(frame->broadcast);

    RS232_Print(" 0x");
    RS232_PrintHex12(frame->controller_addr);
    RS232_Print(" 0x");
    RS232_PrintHex12(frame->peripheral_addr);

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
}

AVCLAN_frame_t *AVCLAN_parseframe(const uint8_t *bytes, uint8_t len) {
  if (len < sizeof(AVCLAN_frame_t))
    return NULL;

  AVCLAN_frame_t *frame = malloc(sizeof(AVCLAN_frame_t) + 1);

  if (!frame)
    return NULL;

  frame->broadcast = *bytes++;
  frame->controller_addr = *(uint16_t *)bytes++;
  bytes++;
  frame->peripheral_addr = *(uint16_t *)bytes++;
  bytes++;
  frame->control = *bytes++;
  frame->length = *bytes++;

  if (frame->length <= (len - 8)) {
    free(frame);
    return NULL;
  } else {
    AVCLAN_frame_t *framedata =
        realloc(frame, sizeof(AVCLAN_frame_t) + frame->length);
    if (!framedata) {
      free(frame);
      return NULL;
    }
    frame = framedata;
    frame->data = (uint8_t *)frame + sizeof(AVCLAN_frame_t);
    for (uint8_t i = 0; i < frame->length; i++) {
      frame->data[i] = *bytes++;
    }
  }

  return frame;
}

uint8_t CheckCmd(const AVCLAN_frame_t *frame, const uint8_t *cmd, uint8_t l) {
  for (uint8_t i = 0; i < l; i++) {
    if (frame->data[i] != *cmd++)
      return 0;
  }
  return 1;
}

void AVCLan_Send_Status() {
  uint8_t STATUS[] = {0x63, 0x31, 0xF1, 0x01, 0x10, 0x01,
                      0x01, 0x00, 0x00, 0x00, 0x80};
  STATUS[6] = *cd_Track;
  STATUS[7] = *cd_Time_Min;
  STATUS[8] = *cd_Time_Sec;
  STATUS[9] = 0;

  AVCLAN_frame_t status = {.broadcast = UNICAST,
                           .controller_addr = CD_ID,
                           .peripheral_addr = HU_ID,
                           .control = 0xF,
                           .length = 11,
                           .data = &STATUS[0]};

  AVCLAN_sendframe(&status);
}

void AVCLan_Register() {
  AVCLAN_frame_t register_frame = {.broadcast = CMD_REGISTER.broadcast,
                                   .controller_addr = CD_ID,
                                   .peripheral_addr = HU_ID,
                                   .control = 0xF,
                                   .length = CMD_REGISTER.length,
                                   .data = (uint8_t *)CMD_REGISTER.data};
  RS232_Print("REG_ST\n");
  AVCLAN_sendframe(&register_frame);
  RS232_Print("REG_END\n");
  // AVCLan_Command( cm_Register );
  answerReq = cm_Init;
  AVCLAN_respond();
}

#ifdef SOFTWARE_DEBUG
uint16_t pulses[100];
uint16_t periods[100];

void AVCLan_Measure() {
  STOPEvent;

  uint8_t tmp = 0;

  RS232_Print(
      "Timing config: F_CPU=" STR(F_CPU) ", TCB_CLKSEL=" STR(TCB_CLKSEL) "\n");
  RS232_Print("Sampling bit (pulse-width and period) timing...\n");

  for (uint8_t n = 0; n < 100; n++) {
    while (pulse_count == tmp) {}
    pulses[n] = pulsewidth;
    periods[n] = period;
    tmp = pulse_count;
  }

  RS232_Print("Pulses:\n");
  for (uint8_t i = 0; i < 100; i++) {
    RS232_PrintHex8(*(((uint8_t *)&pulses[i]) + 1));
    RS232_PrintHex8(*(((uint8_t *)&pulses[i]) + 0));
    RS232_Print("\n");
  }

  RS232_Print("Periods:\n");
  for (uint8_t i = 0; i < 100; i++) {
    RS232_PrintHex8(*(((uint8_t *)&periods[i]) + 1));
    RS232_PrintHex8(*(((uint8_t *)&periods[i]) + 0));
    RS232_Print("\n");
  }
  RS232_Print("\nDone.\n");

  STARTEvent;
}
#endif
