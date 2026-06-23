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

  A start bit is nominally 169 us high followed by 20 us low.

  A bit `0` is dominant on the bus, which is a design choice that affects
  bit/interpretation:
    - Low addresses have priority upon transmission conflicts
    - The broadcast bit is `1` (floating, no effort) for normal communication
    - For acknowledge bits, the receiver extends the logical '0' of the sync
      period to the length of a normal bit `0`. Hence, a NAK (bit `1`) is
      literally the absence of an ACK.

--------------------------------------------------------------------------------------
*/

#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/sfr_defs.h>
#include <stdint.h>

#include "avclan_phy.h"

// F_CPU defined in timing.h and potentially needed by avr-libc (e.g. delay.h)
#include "timing.h"

// Name difference between avr-libc and Microchip pack
#if defined(EVSYS_ASYNCCH00_bm)
  #define EVSYS_ASYNCCH0_0_bm EVSYS_ASYNCCH00_bm
#endif

#define READING_BYTE   GPIOR1
#define READING_NBITS  GPIOR2
#define READING_PARITY GPIOR3

#define TCB_CNTMODE TCB_CNTMODE_PW_gc

volatile uint16_t pulsewidth;

#ifndef NDEBUG
volatile uint8_t pulse_count = 0;
volatile uint16_t period = 0;
#endif

// clang-format off
static inline void AVCLAN_setBusIdle() {
  __asm__ __volatile__(
      "cbi %[vporta_out], 4; \n\t"
      "sbi %[vportc_out], 0; \n\t"
      ::[vporta_out] "I"(_SFR_IO_ADDR(VPORTA_OUT)),
        [vportc_out] "I"(_SFR_IO_ADDR(VPORTC_OUT)));
}
static inline void AVCLAN_setBusDriven() {
  __asm__ __volatile__(
      "sbi %[vporta_out], 4; \n\t"
      "cbi %[vportc_out], 0; \n\t"
      ::[vporta_out] "I"(_SFR_IO_ADDR(VPORTA_OUT)),
        [vportc_out] "I"(_SFR_IO_ADDR(VPORTC_OUT)));
}
// clang-format on

// Mute device TX on AVCLAN bus
void AVCLAN_muteDevice(bool mute) {
  if (mute) {
    // clang-format off
    __asm__ __volatile__("cbi %[vporta_dir], 4; \n\t" // set as INPUT (output values ignored)
                         "cbi %[vportc_dir], 0; \n\t" // set as INPUT (output values ignored)
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

// Set AVC bus to `val` (logical 1 or 0) for `period` ticks of TCB1
static void set_AVC_logic_for(uint8_t val, uint16_t period) {
  TCB1.CNT = 0;
  if (val) {
    AVCLAN_setBusIdle(); // idle bus is logical 1
  } else {
    AVCLAN_setBusDriven();
  }
  while (TCB1.CNT <= period) {};

  return;
}

void AVCLAN_sendbit(avclan_bit_t bit) {
  uint16_t zero_length, one_length;
  switch (bit) {
    case bit_zero:
      zero_length = AVCLAN_BIT0_LOGIC_0;
      one_length = AVCLAN_BIT0_LOGIC_1;
      break;
    case bit_one:
      zero_length = AVCLAN_BIT1_LOGIC_0;
      one_length = AVCLAN_BIT1_LOGIC_1;
      break;
    case bit_start:
      zero_length = AVCLAN_STARTBIT_LOGIC_0;
      one_length = AVCLAN_STARTBIT_LOGIC_1;
      break;
    default: __builtin_unreachable();
  }
  set_AVC_logic_for(0, zero_length);
  set_AVC_logic_for(1, one_length);
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

  AVCLAN_sendbit(bit_zero);
}

/* Returns true if the peripheral sent an ACK bit.
  An ACK bit is a cooperative bit, where the sender starts (drives the bus) a
  sync period, and allows the receiver to drive the bus (or not) to finish a "1"
  bit.
*/
uint8_t AVCLAN_readbit_ACK() {
  TCB1.CNT = 0;                              // Double reset of TCB1.CNT: here
  set_AVC_logic_for(0, AVCLAN_BIT1_LOGIC_0); // And here (within)
  AVCLAN_setBusIdle();                       // Stop driving bus

  while (true) {
    if (!BUS_IS_IDLE && (TCB1.CNT > AVCLAN_READBIT_THRESHOLD))
      break; // ACK
    if (TCB1.CNT > AVCLAN_BIT_LENGTH_MAX)
      return 0; // NAK
  }

  // Check/wait in case we get here before peripheral finishes ACK bit
  while (!BUS_IS_IDLE) {
    if (TCB1.CNT > AVCLAN_BIT_LENGTH_MAX)
      return 0; // NAK
  }
  return 1;
}

// Send `len` bits on the AVCLAN bus; returns the even parity
avclan_bit_t AVCLAN_sendbitsi(const uint8_t *bits, int8_t len) {
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
      avclan_bit_t bit = (b & 0x80) != 0;
      parity += (uint8_t)bit;
      AVCLAN_sendbit(bit);
      b <<= 1;
    }
    len_mod8 = 8;
    b = *--bits;
  }
  return (parity & 1);
}

// Send `len` bits on the AVCLAN bus; returns the even parity
avclan_bit_t AVCLAN_sendbitsl(const uint16_t *bits, int8_t len) {
  return AVCLAN_sendbitsi((const uint8_t *)bits + 1, len);
}

avclan_bit_t AVCLAN_sendbyte(const uint8_t *byte) {
  uint8_t b = *byte;
  uint8_t parity = 0;

  for (uint8_t nbits = 8; nbits > 0; nbits--) {
    avclan_bit_t bit = (b & 0x80) != 0;
    parity += (uint8_t)bit;
    AVCLAN_sendbit(bit);
    b <<= 1;
  }
  return (parity & 1);
}

ISR(TCB0_INT_vect) {
  // #ifndef NDEBUG
  //   pulse_count++;
  //   // PW mode fires on falling edge; measure period as TCB1 delta between
  //   // consecutive falling edges (equivalent to FRQPW's rising-to-rising).
  //   static uint16_t last_tcb1 = 0;
  //   uint16_t cur_tcb1 = TCB1.CNT;
  //   period = cur_tcb1 - last_tcb1;
  //   last_tcb1 = cur_tcb1;
  // #endif

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

// Read `len` bits on the AVCLAN bus; returns the even parity
uint8_t AVCLAN_readbitsi(uint8_t *bits, uint8_t len) {
  cli();
  READING_BYTE = 0;
  READING_PARITY = 0;
  READING_NBITS = len;
  sei();

  TCB1.CNT = 0;
  while (READING_NBITS) {
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
  while (READING_NBITS) {
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

void AVCLAN_phyInit() {
  // Set pin 6 and 7 as input
  PORTA.DIRCLR = (PIN6_bm | PIN7_bm);
  // Disable input buffer; recommended when using AC
  PORTA.PIN6CTRL = PORT_ISC_INPUT_DISABLE_gc;
  // PA7/AINN1(-) additionally gets a pull-up to help prevent the comparator
  // latching high (ie false "driven" bus)
  PORTA.PIN7CTRL = PORT_PULLUPEN_bm | PORT_ISC_INPUT_DISABLE_gc;

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

  AVCLAN_setBusIdle();

  AVCLAN_muteDevice(false); // unmute AVCLAN bus TX
}
