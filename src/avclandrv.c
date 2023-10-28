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
#include <string.h>

#define VAR_DECLS
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

#define MAX_SEND_ATTEMPTS 3

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

// answers
uint8_t lancheck_resp[] = {0x00, 0x01, 0x00, 0xFF};
const uint8_t list_functions_resp[] = {0x00, dev_COMM_CTRL, dev_COMM_v1,
                                       List_Functions_Resp, dev_CD_CHANGER};
uint8_t ping_resp[] = {0x00, dev_COMM_CTRL, dev_COMM_v1, Ping_Resp, 0xFF, 0x00};
uint8_t function_change_resp[] = {0x00, dev_CD_CHANGER, dev_COMM_v1, 0xFF,
                                  0x01};
uint8_t cdstatus_resp[] = {
    dev_CD_CHANGER, dev_STATUS, Report, 0x01, cd_SEEKING_TRACK, 0x01, 0x00,
    0xFF,           0x7F,       0x00,   0xc0};

uint8_t AVCLAN_handleframe(const AVCLAN_frame_t *frame);
void AVCLAN_updateCDStatus();

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
  cd_status.state = cd_SEEKING_TRACK;
  cd_status.disk_random = 0;
  cd_status.random = 0;
  cd_status.disk_repeat = 0;
  cd_status.repeat = 0;
  cd_status.scan = 0;
  cd_status.flags2 = 0xC0;

  cd_status.track = 1;
  cd_status.mins = 0xFF;
  cd_status.secs = 0x7F;

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

  if (!AVCLAN_ismuted())
    AVCLAN_handleframe(&frame);

  answerReq = cm_Null;
  return 1;
}

uint8_t AVCLAN_sendframe(const AVCLAN_frame_t *frame) {
  if (AVCLAN_ismuted())
    return 1;

  STOPEvent;

  uint8_t parity = 0;

  // wait for free line
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

const AVCLAN_frame_t *frameQueue[4];

static inline uint8_t qFull() {
  return ((qWrite - qRead) == sizeof(frameQueue));
}

static inline uint8_t qMask(uint8_t pos) {
  return pos & (sizeof(frameQueue) - 1);
}

uint8_t qPush(const AVCLAN_frame_t *frame) {
  if (qFull())
    return 1;

  frameQueue[qMask(qWrite++)] = frame;

  return 0;
}

const AVCLAN_frame_t *qPeek() {
  if (qEmpty())
    return NULL;

  return frameQueue[qMask(qRead)];
}

const AVCLAN_frame_t *qPop() {
  if (qEmpty())
    return NULL;

  return frameQueue[qMask(qRead++)];
}

uint8_t AVCLAN_handleframe(const AVCLAN_frame_t *frame) {
  uint8_t respond = 0;
  AVCLAN_frame_t *resp = malloc(sizeof(AVCLAN_frame_t));

  if (!resp)
    return NULL;

  resp->controller_addr = DEVICE_ADDR;
  resp->control = 0xF;

  if (!frame->broadcast) {
    // peripheral_addr will be 0xFFF or 0x1FF based on all currently known
    // examples
    // if (frame->peripheral_addr == 0xFFF || frame->peripheral_addr == 0x1FF) {
    if (frame->data[0] == 0) {
      if (frame->data[1] == dev_COMM_CTRL) {
        switch (frame->data[2]) {
          case Lancheck_Scan_Req:
            lancheck_resp[3] = Lancheck_Scan_Resp;
            goto GROUPED;
          case Lancheck_Req:
            lancheck_resp[3] = Lancheck_Resp;
            goto GROUPED;
          case Lancheck_End_Req:
            lancheck_resp[3] = Lancheck_End_Resp;
            goto GROUPED;
          default:
            break;
          GROUPED:
            resp->broadcast = UNICAST;
            resp->peripheral_addr = HU_ADDR;
            resp->length = sizeof(lancheck_resp);
            resp->data = (uint8_t *)lancheck_resp;
            respond = 1;
        }
      }
    } else if (frame->data[0] == dev_COMM_v1) {
      if (frame->data[1] == dev_COMM_CTRL) {
        switch (frame->data[2]) {
          case Advertise_Function:
            if (frame->data[3] == dev_CD_CHANGER)
              CD_Mode = stPlay;
            else
              CD_Mode = stStop;
            break;
          case Ping_Req:
            resp->broadcast = UNICAST;
            resp->peripheral_addr = HU_ADDR;
            resp->length = sizeof(ping_resp);
            ping_resp[4] = frame->data[3];
            resp->data = (uint8_t *)&ping_resp;
            respond = 1;
            break;
          case List_Functions_Req:
            resp->broadcast = UNICAST;
            resp->peripheral_addr = HU_ADDR;
            resp->length = sizeof(list_functions_resp);
            resp->data = (uint8_t *)&list_functions_resp;
            respond = 1;
            break;
          // case Restart_Lan:
          //   break;
          default:
        }
      }
    }
    // }
  } else if (frame->peripheral_addr == DEVICE_ADDR) { // unicast to CD changer
    if (frame->data[0] == 0) {
      switch (frame->data[1]) {
        case dev_COMM_v1:
          switch (frame->data[2]) {
            case dev_CD_CHANGER:
              switch (frame->data[3]) {
                case Enable_Function_Req:
                  function_change_resp[3] = Enable_Function_Resp;
                  cd_status.state = cd_SEEKING;
                  cd_status.flags2 = 0x80;
                  *cd_Time_Min = 0x00;
                  *cd_Time_Sec = 0x00;
                  CD_Mode = stPlay;
                  answerReq = cm_CDStatus;
                  goto GROUPED2;
                case Disable_Function_Req:
                  function_change_resp[3] = Disable_Function_Resp;
                  CD_Mode = stStop;
                  cd_status.state = 0;
                  *cd_Time_Min = 0x00;
                  *cd_Time_Sec = 0x00;
                  answerReq = cm_CDStatus;
                  goto GROUPED2;
                // case 0x80:
                //   act = Inserted_CD;
                //   goto GROUPED;
                default:
                  break;
                GROUPED2:
                  resp->broadcast = UNICAST;
                  resp->peripheral_addr = HU_ADDR;
                  resp->length = sizeof(function_change_resp);
                  resp->data = (uint8_t *)function_change_resp;
                  respond = 1;
              }
            default:
          }
          break;
        case dev_CMD_SW:
        case dev_STATUS:
          if (frame->data[2] == dev_CD_CHANGER) {
            switch (frame->data[3]) {
              case Request_Report:
                cdstatus_resp[2] = Report;
                goto GROUPED3;
              case Request_Report2:
                cdstatus_resp[2] = Report2;
                goto GROUPED3;
              case Request_Loader2:
                cdstatus_resp[2] = Report_Loader2;
                goto GROUPED3;
              default:
                break;
              GROUPED3:
                memcpy(&cdstatus_resp[3], &cd_status, sizeof(cd_status));
                resp->broadcast = BROADCAST;
                resp->peripheral_addr = 0x1FF;
                resp->length = sizeof(function_change_resp);
                resp->data = (uint8_t *)function_change_resp;
                respond = 1;
            }
          }
          break;
        default:
      }
    }
  }

  if (!respond) {
    free(resp);
  } else {
    qPush(resp);
  }

  return respond;
}

uint8_t AVCLAN_respond() {
  uint8_t r = 0;
  if (!qEmpty()) {
    const AVCLAN_frame_t *resp = qPeek();
    for (uint8_t i = 0; i < MAX_SEND_ATTEMPTS; i++) {
      r = AVCLAN_sendframe(resp);
      if (!r) { // Send succeeded
        resp = qPop();
        free((AVCLAN_frame_t *)resp);
        break;
      }
    }
    if (r) { // Sending failed all attempts; give up sending frame
      resp = qPop();
      free((AVCLAN_frame_t *)resp);
    }
  } else if (!answerReq) {
    AVCLAN_frame_t frame = {.broadcast = UNICAST,
                            .controller_addr = DEVICE_ADDR,
                            .peripheral_addr = HU_ADDR,
                            .control = 0xF,
                            .length = 0};

    switch (answerReq) {
      case cm_CDStatus:
        AVCLAN_updateCDStatus();
    }
  }

  answerReq = cm_Null;
  return r;
}

void AVCLAN_printframe(const AVCLAN_frame_t *frame, uint8_t binary) {
  if (binary) {
    uint8_t buffer[8];
    buffer[0] = 0x10; // Data Link Escape, signaling binary data forthcoming
    buffer[1] = frame->broadcast;

    // Send addresses in big-endian order
    buffer[2] = *(((uint8_t *)&frame->controller_addr) + 1);
    buffer[3] = *(((uint8_t *)&frame->controller_addr) + 0);
    buffer[4] = *(((uint8_t *)&frame->peripheral_addr) + 1);
    buffer[5] = *(((uint8_t *)&frame->peripheral_addr) + 0);

    buffer[6] = frame->control;
    buffer[7] = frame->length;
    RS232_sendbytes((uint8_t *)&buffer, 8);
    RS232_sendbytes(frame->data, frame->length);

    buffer[0] = 0x17; // End of transmission block
    buffer[1] = 0x0D; // \r
    buffer[2] = 0x0A; // \n
    RS232_sendbytes((uint8_t *)&buffer, 3);
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

void AVCLAN_updateCDStatus() {
  if (CD_Mode) {
    if (cd_status.state != cd_PLAYBACK) {
      cd_status.state = cd_PLAYBACK;
      answerReq = cm_CDStatus;
    }

    if (answerReq == cm_CDStatus) {
      cdstatus_resp[2] = Report;
      memcpy(&cdstatus_resp[3], &cd_status, sizeof(cd_status));

      AVCLAN_frame_t status = {.broadcast = BROADCAST,
                               .controller_addr = DEVICE_ADDR,
                               .peripheral_addr = 0x1FF,
                               .control = 0xF,
                               .length = sizeof(cdstatus_resp),
                               .data = (uint8_t *)&cdstatus_resp};

      AVCLAN_sendframe(&status);
    }
  }
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
