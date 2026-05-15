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
    - The broadcast bit is `1` (floating, no effort) for normal communication
    - For acknowledge bits, the receiver extends the logical '0' of the sync
      period to the length of a normal bit `0`. Hence, a NAK (bit `1`) is
      literally the absence of an ACK.

  No acknowledge bits are sent for broadcast frames.

--------------------------------------------------------------------------------------
*/

#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/sfr_defs.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "avclandrv.h"
#include "com232.h"

// F_CPU defined in timing.h and potentially needed by avr-libc (e.g. delay.h)
#include "timing.h"

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

AVCLAN_CD_Status_t cd_status;

uint8_t *cd_Track;
uint8_t *cd_Time_Min;
uint8_t *cd_Time_Sec;

cd_modes CD_Mode;

#ifdef SOFTWARE_DEBUG
volatile uint8_t pulse_count = 0;
volatile uint16_t period = 0;
#endif

volatile uint16_t pulsewidth;

// answers
uint8_t lancheck_resp[] = {0x00, dev_COMM_CTRL, dev_LAN, 0xFF, 0xFF};
const uint8_t list_functions_resp[] = {0x00, dev_COMM_CTRL, dev_COMM_v1,
                                       List_Functions_Resp, dev_CD_CHANGER};
uint8_t ping_resp[] = {0x00, dev_COMM_CTRL, dev_COMM_v1, Ping_Resp, 0xFF, 0x00};
uint8_t function_change_resp[] = {0x00, dev_CD_CHANGER, dev_COMM_v1, 0xFF,
                                  0x01};

#define STATUS_REPORT_DATA                                                     \
  {dev_CD_CHANGER,                                                             \
   dev_STATUS,                                                                 \
   Status_Report,                                                              \
   0x01,                                                                       \
   cd_SEEKING_TRACK,                                                           \
   0x01,                                                                       \
   0x00,                                                                       \
   0xFF,                                                                       \
   0x7F,                                                                       \
   0x00,                                                                       \
   0x80}

uint8_t cdstatus_resp[] = STATUS_REPORT_DATA;

uint8_t cdinitreport_resp[] = {
    dev_CD_CHANGER, dev_STATUS, Initial_Report_Response, 0x01, 0x31, 0x10,
    0x01,           0x01};

uint8_t cdloading_resp[] = {dev_CD_CHANGER,
                            dev_STATUS,
                            Loading_Status_Report,
                            0x00,
                            0x01,
                            0x00,
                            0x01,
                            0x00,
                            0x01,
                            0x02};

/* Disable serial and periodic interrupts during AVCLAN reads.
  Not using cli() because AVCLAN reads depend on other interrupts. */
static inline void stopEvent() {
  cbi(RTC.PITINTCTRL, RTC_PI_bp);
  cbi(USART0.CTRLA, USART_RXCIE_bp);
}

// Re-enable serial and periodic interrupts.
static inline void startEvent() {
  if (AVCLAN_isPlaying()) // Reenable PIT interrupt if currently playing
    sbi(RTC.PITINTCTRL, RTC_PI_bp);
  sbi(USART0.CTRLA, USART_RXCIE_bp);
}

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

// Returns true if device TX is muted on AVCLAN bus
static inline uint8_t AVCLAN_ismuted() {
  return (((VPORTA_DIR & PIN4_bm) | (VPORTA_DIR & PIN0_bm)) == 0);
}

// Mute device TX on AVCLAN bus
void AVCLAN_muteDevice(uint8_t mute) {
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

// Sets CD_mode to play and resets timer count (so that the next interrupt is in
// 1 sec)
void AVCLAN_startPlaying() {
  CD_Mode = stPlay;
  cli();
  loop_until_bit_is_clear(RTC_PITSTATUS, RTC_CNTBUSY_bp);
  RTC.CNT = 0;
  sbi(RTC.PITINTCTRL, RTC_PI_bp);
  sei();
}

// Sets CD_mode to play and resets timer count (so that the next interrupt is in
// 1 sec)
void AVCLAN_stopPlaying() {
  CD_Mode = stStop;
  cbi(RTC.PITINTCTRL, RTC_PI_bp);
}

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

  AVCLAN_setBusIdle();

  AVCLAN_muteDevice(0); // unmute AVCLAN bus TX

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

/* Pack a 0–99 count into 2-digit BCD. Values >99 (sentinels such as 0xFF /
   0x7F meaning "no time") pass through unchanged so they survive the wire
   round-trip. */
static uint8_t toBCD(uint8_t x) {
  if (x > 99)
    return x;
  return (uint8_t)(((x / 10) << 4) | (x % 10));
}

// Copy cd_status to a wire response, applying BCD conversion to time fields.
static void serializeCDStatus(uint8_t *dst) {
  AVCLAN_CD_Status_t wire = cd_status;
  wire.mins = toBCD(wire.mins);
  wire.secs = toBCD(wire.secs);
  memcpy(dst, &wire, sizeof(wire));
}

uint8_t AVCLAN_isPlaying() { return (CD_Mode == stPlay); }

void AVCLAN_incrementTime() {
  // Sentinel values (>99) mean "no time"; leave them alone until setTime()
  // replaces them with a real count.
  if (*cd_Time_Sec > 99)
    return;
  if (*cd_Time_Sec == 59) {
    *cd_Time_Sec = 0;
    if (*cd_Time_Min == 99)
      *cd_Time_Min = 0;
    else
      (*cd_Time_Min)++;
  } else
    (*cd_Time_Sec)++;
}

void AVCLAN_setTime(uint8_t mins, uint8_t secs) {
  *cd_Time_Min = mins;
  *cd_Time_Sec = secs;
}

// Set AVC bus to `val` (logical 1 or 0) for `period` ticks of TCB1
void set_AVC_logic_for(uint8_t val, uint16_t period) {
  TCB1.CNT = 0;
  if (val) {
    AVCLAN_setBusIdle(); // idle bus is logical 1
  } else {
    AVCLAN_setBusDriven();
  }
  while (TCB1.CNT <= period) {};

  return;
}

typedef enum avclan_bit : uint8_t {
  bit_zero = 0x00,
  bit_one = 0x01,
  bit_start = 0x10
} avclan_bit_t;

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
  TCB1.CNT = 0;
  set_AVC_logic_for(0, AVCLAN_BIT1_LOGIC_0);
  AVCLAN_setBusIdle(); // Stop driving bus

  while (1) {
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

#define AVCLAN_sendbits(bits, len)                                             \
  _Generic((bits),                                                             \
      const uint16_t *: AVCLAN_sendbitsl,                                      \
      uint16_t *: AVCLAN_sendbitsl,                                            \
      const uint8_t *: AVCLAN_sendbitsi,                                       \
      uint8_t *: AVCLAN_sendbitsi)(bits, len)

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

uint8_t AVCLAN_readframe(AVCLAN_frame_t *frame, log_t print) {
  struct errtype {
    // Error enum is ordered such that a lower numeric value corresponds to more
    // successful read
    enum : uint8_t {
      NO_ERROR = 0x00,
      BAD_DATA_PARITY = 0x01,
      BAD_LENGTH_RANGE,
      BAD_LENGTH_PARITY,
      BAD_PERIPHERAL_PARITY,
      BAD_CONTROLLER_PARITY,
      BAD_CONTROL_PARITY,
      STARTBIT_TOO_SHORT,
      STARTBIT_TOO_LONG,
    } errno;
    union {
      uint8_t val; // BAD_LENGTH_RANGE: the out-of-range length value
      struct {
        uint16_t read_val;
        uint8_t parity; // received (bad) parity bit
      };
    };
  } err = {0};

  stopEvent(); // disable timer1 interrupt

  uint8_t tmp = 0;

  uint16_t startbitlen = TCB1.CNT = 0;
  while (!BUS_IS_IDLE) {
    startbitlen = TCB1.CNT;
    if (startbitlen > (uint16_t)AVCLAN_STARTBIT_LOGIC_0 * 1.2) {
      err.errno = STARTBIT_TOO_LONG;
      goto handle_err;
    }
  }
  if (startbitlen < (uint16_t)(AVCLAN_STARTBIT_LOGIC_0 * 0.8)) {
    err.errno = STARTBIT_TOO_SHORT;
    // We missed the beginning of this message; wait for it to finish (bus
    // continuously idle for >1 bit length) before returning, so we don't have
    // multiple false-starts while the in-progress message keeps sending more
    // bits.
    TCB1.CNT = 0;
    while (TCB1.CNT < (uint16_t)(AVCLAN_BIT_LENGTH_MAX * 1.2)) {
      if (!BUS_IS_IDLE)
        TCB1.CNT = 0;
    }
    goto handle_err;
  }
  // Otherwise that was a start bit

  AVCLAN_readbits(&frame->broadcast, 1);

  uint8_t parity = AVCLAN_readbits(&frame->controller_addr, 12);
  AVCLAN_readbits(&tmp, 1);
  if (parity != (tmp &= 1)) {
    err.errno = BAD_CONTROLLER_PARITY;
    if (print.verbose) {
      err.read_val = frame->controller_addr;
      err.parity = tmp;
    }
    goto handle_err;
  }

  parity = AVCLAN_readbits(&frame->peripheral_addr, 12);
  AVCLAN_readbits(&tmp, 1);
  if (parity != (tmp &= 1)) {
    err.errno = BAD_PERIPHERAL_PARITY;
    if (print.verbose) {
      err.read_val = frame->peripheral_addr;
      err.parity = tmp;
    }
    goto handle_err;
  }

  uint8_t shouldACK =
      !AVCLAN_ismuted() && (frame->peripheral_addr == DEVICE_ADDR);

  if (shouldACK)
    AVCLAN_sendbit_ACK();
  else
    AVCLAN_readbits(&tmp, 1);

  parity = AVCLAN_readbits(&frame->control, 4);
  AVCLAN_readbits(&tmp, 1);
  if (parity != (tmp &= 1)) {
    err.errno = BAD_CONTROL_PARITY;
    if (print.verbose) {
      err.read_val = frame->control;
      err.parity = tmp;
    }
    goto handle_err;
  } else if (shouldACK) {
    AVCLAN_sendbit_ACK();
  } else {
    AVCLAN_readbits(&tmp, 1);
  }

  parity = AVCLAN_readbyte(&frame->length);
  AVCLAN_readbits(&tmp, 1);
  if (parity != (tmp &= 1)) {
    err.errno = BAD_LENGTH_PARITY;
    if (print.verbose) {
      err.read_val = frame->length;
      err.parity = tmp;
    }
    goto handle_err;
  } else if (shouldACK) {
    AVCLAN_sendbit_ACK();
  } else {
    AVCLAN_readbits(&tmp, 1);
  }

  if (frame->length == 0 || frame->length > MAXMSGLEN) {
    err.errno = BAD_LENGTH_RANGE;
    err.val = frame->length;
    goto handle_err;
  }

  for (uint8_t i = 0; i < frame->length; i++) {
    parity = AVCLAN_readbyte(&frame->data[i]);
    AVCLAN_readbits(&tmp, 1);
    if (parity != (tmp &= 1)) {
      err.errno = BAD_DATA_PARITY;
      if (print.verbose) {
        err.read_val = frame->data[i];
        err.parity = tmp;
      }
      goto handle_err;
    } else if (shouldACK) {
      AVCLAN_sendbit_ACK();
    } else {
      AVCLAN_readbits(&tmp, 1);
    }
  }

  if (0) {
  handle_err:;
    startEvent();
    RS232_Print("ERR(read): ");
    switch (err.errno) {
      case STARTBIT_TOO_SHORT: RS232_Print("start bit too short"); break;
      case STARTBIT_TOO_LONG: RS232_Print("start bit too long"); break;
      case BAD_CONTROLLER_PARITY:
        RS232_Print("reading controller addr.");
        goto VERBOSE;
      case BAD_PERIPHERAL_PARITY:
        RS232_Print("reading peripheral addr.");
        goto VERBOSE;
      case BAD_CONTROL_PARITY: RS232_Print("reading control"); goto VERBOSE;
      case BAD_LENGTH_PARITY: RS232_Print("reading length"); goto VERBOSE;
      case BAD_LENGTH_RANGE:
        RS232_Print("bad length 0x");
        RS232_PrintHex4(err.val);
        break;
      case BAD_DATA_PARITY: RS232_Print("reading data"); goto VERBOSE;
      case NO_ERROR:
        __builtin_unreachable();
      VERBOSE:
        if (print.verbose) {
          RS232_Print("; read 0x");
          RS232_PrintHex(err.read_val);
          RS232_Print(" and got bad parity ");
          RS232_PrintHex4(err.parity);
        }
    }
    RS232_Print("\n");
  } else {
    startEvent();
  }

  // Only print if some data has been correctly recieved
  if (print.print && (err.errno < STARTBIT_TOO_SHORT)) {
    if (err.errno > BAD_DATA_PARITY)
      frame->length = 0;
    AVCLAN_printframe(frame, print.binary);
  }

  return err.errno;
}

uint8_t AVCLAN_sendframe(const AVCLAN_frame_t *frame, log_t print) {
  struct errtype {
    // Error enum is ordered such that a lower numeric value corresponds to more
    // success
    enum : uint8_t {
      NO_ERROR = 0x00,
      NAK_DATA = 0x01,
      NAK_MESSAGE_LENGTH,
      NAK_CONTROL,
      NAK_ADDRESS,
      BUSY,
      MUTED,
    } errno;
    uint8_t val;
  } err = {0};

  if (AVCLAN_ismuted()) {
    err.errno = MUTED;
    goto handle_err;
  }

  stopEvent();

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
    err.errno = BUSY;
    goto handle_err;

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
    AVCLAN_sendbit(bit_start);
  }
  AVCLAN_sendbits((uint8_t *)&frame->broadcast, 1);

  avclan_bit_t parity = AVCLAN_sendbits(&frame->controller_addr, 12);
  AVCLAN_sendbit(parity);

  parity = AVCLAN_sendbits(&frame->peripheral_addr, 12);
  AVCLAN_sendbit(parity);

  if (frame->broadcast && !AVCLAN_readbit_ACK()) {
    err.errno = NAK_ADDRESS;
    goto handle_err;
  }

  parity = AVCLAN_sendbits(&frame->control, 4);
  AVCLAN_sendbit(parity);

  if (frame->broadcast && !AVCLAN_readbit_ACK()) {
    err.errno = NAK_CONTROL;
    goto handle_err;
  }

  parity = AVCLAN_sendbyte(&frame->length); // data length
  AVCLAN_sendbit(parity);

  if (frame->broadcast && !AVCLAN_readbit_ACK()) {
    err.errno = NAK_MESSAGE_LENGTH;
    goto handle_err;
  }

  for (uint8_t i = 0; i < frame->length; i++) {
    parity = AVCLAN_sendbyte(&frame->data[i]);
    AVCLAN_sendbit(parity);
    // Based on the µPD6708 datasheet, ACK bit for broadcast doesn't seem
    // necessary (i.e. This deviates from the previous broadcast specific
    // function that sent an extra `1` bit after each byte/parity)
    if (frame->broadcast && !AVCLAN_readbit_ACK()) {
      err.errno = NAK_DATA;
      err.val = i;
      goto handle_err;
    }
    // else
    //   AVCLAN_sendbit_1();
  }

  // back to read mode
  if (0) {
  handle_err:;
    startEvent();
    RS232_Print("Error");
    switch (err.errno) {
      case MUTED: RS232_Print(": Device muted"); break;
      case BUSY: RS232_Print(": Busy bus"); break;
      case NAK_ADDRESS:
      case NAK_CONTROL:
      case NAK_MESSAGE_LENGTH:
      case NAK_DATA:
        RS232_Print(" NAK: ");
        switch (err.errno) {
          case NAK_ADDRESS: RS232_Print("address"); break;
          case NAK_CONTROL: RS232_Print("Control"); break;
          case NAK_MESSAGE_LENGTH: RS232_Print("Message length"); break;
          case NAK_DATA:
            RS232_Print(" data[");
            RS232_PrintDec(err.val);
            RS232_Print("]");
            break;
          case NO_ERROR:
          case MUTED:
          case BUSY: __builtin_unreachable();
        }
        break;
      case NO_ERROR: __builtin_unreachable();
    }
    RS232_Print("\n");
  } else {
    startEvent();
  }

  if (print.print)
    AVCLAN_printframe(frame, print.binary);

  return err.errno;
}

response_t AVCLAN_handleframe(const AVCLAN_frame_t *in, AVCLAN_frame_t *out) {
  response_t respond = r_Nothing;

  if (AVCLAN_ismuted() || in->length < 3)
    return respond;

  out->controller_addr = DEVICE_ADDR;
  out->control = 0xF;

  const uint16_t peripheral_addr = in->peripheral_addr;
  const uint8_t *data = in->data;
  const uint8_t b0 = *data++;
  const uint8_t b1 = *data++;
  const uint8_t b2 = *data++;
  uint8_t b3 = 0;
  if (in->length > 3) // the shortest known/valid messages are 3 bytes long
    b3 = *data++;
  uint8_t from;

  // BROADCAST
  if (in->broadcast == BROADCAST) {
    // skip confirming peripheral_addr, because it  will be 0xFFF or 0x1FF based
    // on all currently known examples
    switch (b0 /* "from" device */) {
      case dev_LAN:
        switch (b1 /* "to" device */) {
          case dev_COMM_CTRL:
            switch (b2 /* device action */) {
              case Lancheck_Scan_Req:
                lancheck_resp[3] = Lancheck_Scan_Resp;
                lancheck_resp[4] = 0x01;
                out->length = sizeof(lancheck_resp);
                goto LAN_RESPONSE;
              case Lancheck_Req:
                lancheck_resp[3] = Lancheck_Resp;
                lancheck_resp[4] = 0x00;
                out->length = sizeof(lancheck_resp);
                goto LAN_RESPONSE;
              case Lancheck_End_Req:
                lancheck_resp[3] = Lancheck_End_Resp;
                out->length = sizeof(lancheck_resp) - 1;
                goto LAN_RESPONSE;
              default:
                break;
              LAN_RESPONSE:
                out->broadcast = UNICAST;
                out->peripheral_addr = HU_ADDR;
                memcpy(out->data, lancheck_resp, sizeof(lancheck_resp));
                respond = r_Handled;
            }
            break;
          default:
        }
        break;
      case dev_COMM_v1:
      case dev_COMM_v2:
        if (b1 /* "to" device */ == dev_COMM_CTRL) {
          switch (b2 /* device action */) {
            case Current_Function:
              if ((b3 == dev_CD_CHANGER) && !AVCLAN_isPlaying()) {
                cd_status.state = cd_SEEKING | cd_SEEKING_TRACK;
                cd_status.flags2 = 0x80;
                AVCLAN_startPlaying();
                AVCLAN_generateStatus(out);
                respond = r_NormalizeState;
              }
              break;
            case Ping_Req:
              out->broadcast = UNICAST;
              out->peripheral_addr = HU_ADDR;
              out->length = sizeof(ping_resp);
              ping_resp[4] = b3;
              memcpy(out->data, ping_resp, sizeof(ping_resp));
              respond = r_Handled;
              break;
            case List_Functions_Req:
              out->broadcast = UNICAST;
              out->peripheral_addr = HU_ADDR;
              out->length = sizeof(list_functions_resp);
              memcpy(out->data, list_functions_resp,
                     sizeof(list_functions_resp));
              respond = r_Handled;
              break;
            // case Restart_Lan:
            //   break;
            default:
          }
        }
        break;
      default:
    }
  } else if (peripheral_addr == DEVICE_ADDR) { // unicast to CD changer
    if (b0 == 0) { // unicasts begin with a zero-byte
      from = b1;
      switch (from) {
        case dev_COMM_v1:
        case dev_COMM_v2:
          switch (b2 /* "to" device */) {
            case dev_CD_CHANGER:
              switch (b3 /* device action */) {
                case Enable_Function_Req:
                  function_change_resp[3] = Enable_Function_Resp;
                  cd_status.state = cd_SEEKING | cd_SEEKING_TRACK;
                  cd_status.flags2 = 0xc0;
                  // *cd_Time_Min = 0xff;
                  // *cd_Time_Sec = 0x7f;
                  AVCLAN_startPlaying();
                  respond = r_StartPlaying;
                  goto FUNCTION_CHANGE_RESPONSE;
                case Disable_Function_Req:
                  AVCLAN_stopPlaying();
                  function_change_resp[3] = Disable_Function_Resp;
                  cd_status.state = cd_PLAYBACK | cd_SEEKING_TRACK;
                  // *cd_Time_Min = 0x00;
                  // *cd_Time_Sec = 0x00;
                  cd_status.flags2 = 0x80;
                  respond = r_StatusReport;
                  goto FUNCTION_CHANGE_RESPONSE;
                default:
                  break;
                FUNCTION_CHANGE_RESPONSE:
                  out->broadcast = UNICAST;
                  out->peripheral_addr = HU_ADDR;
                  out->length = sizeof(function_change_resp);
                  memcpy(out->data, function_change_resp,
                         sizeof(function_change_resp));
              }
              break;
            default:
          }
          break;
        case dev_CMD_SW:
          switch (b2 /* "to" device */) {
            case dev_CD_CHANGER:
              switch (b3 /* device action */) {
                case Initial_Report_Request:
                  out->length = sizeof(cdinitreport_resp);
                  memcpy(out->data, cdinitreport_resp,
                         sizeof(cdinitreport_resp));
                  out->data[1] = from; // respond to device that requested
                  goto CMD_SW_RESPONSE;
                case Playback_Request:
                  out->data[1] = from; // respond to device that requested
                  out->data[2] = Playback_Report;
                  out->length = sizeof(cdstatus_resp);
                  serializeCDStatus(&out->data[3]);
                  goto CMD_SW_RESPONSE;
                case Loading_Request2:
                  out->length = sizeof(cdloading_resp);
                  memcpy(out->data, cdloading_resp, sizeof(cdloading_resp));
                  out->data[1] = from;
                  out->data[2] = Loading_Response2;
                  goto CMD_SW_RESPONSE;
                case Track_Seek_Up:
                  cd_status.state = cd_SEEKING_TRACK;
                  (*cd_Track)++;
                  *cd_Time_Min = 0xff;
                  *cd_Time_Sec = 0x7f;
                  cd_status.scan = 1;
                  cd_status.flags2 = 0xc0;
                  respond = r_TrackChange;
                  AVCLAN_generateStatus(out);
                  break;
                case Track_Seek_Down:
                  cd_status.state = cd_SEEKING_TRACK;
                  (*cd_Track)--;
                  *cd_Time_Min = 0xff;
                  *cd_Time_Sec = 0x7f;
                  cd_status.scan = 1;
                  cd_status.flags2 = 0xc0;
                  respond = r_TrackChange;
                  AVCLAN_generateStatus(out);
                  break;
                default:
                  break;
                CMD_SW_RESPONSE:
                  out->broadcast = UNICAST;
                  out->peripheral_addr = HU_ADDR;
              }
              break;
            default:
          }
          break;
        case dev_STATUS:
          switch (b2 /* "to" device */) {
            case dev_CD_CHANGER:
              switch (b3 /* device action */) {
                case Initial_Report_Request:
                  out->length = sizeof(cdinitreport_resp);
                  memcpy(out->data, cdinitreport_resp,
                         sizeof(cdinitreport_resp));
                  out->data[1] = from; // respond to device that requested
                  goto STATUS_RESPONSE;
                case Playback_Request:
                  out->data[1] = from; // respond to device that requested
                  out->data[2] = Playback_Report;
                  out->length = sizeof(cdstatus_resp);
                  serializeCDStatus(&out->data[3]);
                  goto STATUS_RESPONSE;
                case Loading_Request2:
                  out->length = sizeof(cdloading_resp);
                  memcpy(out->data, cdloading_resp, sizeof(cdloading_resp));
                  out->data[1] = from;
                  out->data[2] = Loading_Response2;
                  goto STATUS_RESPONSE;
                default:
                  break;
                STATUS_RESPONSE:
                  out->broadcast = UNICAST;
                  out->peripheral_addr = HU_ADDR;
              }
              break;
            default:
          }
          break;
        default:
      }
    }
  }

  return respond;
}

uint8_t AVCLAN_tryrespond(const AVCLAN_frame_t *resp) {
  uint8_t r = 0;
  for (uint8_t i = 0; i < MAX_SEND_ATTEMPTS; i++) {
    r = AVCLAN_sendframe(resp, (log_t){0});
    if (!r) // Send succeeded
      break;
  }

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

uint8_t AVCLAN_parseframe(const uint8_t *bytes, uint8_t len,
                          AVCLAN_frame_t *frame) {
  struct errtype {
    enum : uint8_t {
      TOO_SHORT = 0x01,
      MISMATCH_LENGTH,
      LENGTH_TOO_BIG,
    } errno;
    uint8_t val;
  } err = {0};

  if (len < sizeof(AVCLAN_frame_t)) {
    err.errno = TOO_SHORT;
    goto handle_err;
  }
  const uint8_t *last = bytes + len;

  frame->broadcast = *bytes++;
  frame->controller_addr = *(uint16_t *)bytes++;
  bytes++;
  frame->peripheral_addr = *(uint16_t *)bytes++;
  bytes++;
  frame->control = *bytes++;
  frame->length = *bytes++;

  if (frame->length > MAXMSGLEN) {
    err.errno = LENGTH_TOO_BIG;
    err.val = frame->length;
    goto handle_err;
  }

  if ((bytes + frame->length) <= last) {
    memcpy(frame->data, bytes, frame->length);
  } else {
    err.errno = MISMATCH_LENGTH;
    goto handle_err;
  }

  if (0) {
  handle_err:;
    RS232_Print("ERR(parse): ");
    switch (err.errno) {
      case TOO_SHORT:
        RS232_Print("not enough bytes too fill AVCLAN frame");
        break;
      case MISMATCH_LENGTH:
        RS232_Print("frame->length is longer than remaining data");
        break;
      case LENGTH_TOO_BIG:
        RS232_Print("frame->length exceeds MAXMSGLEN: 0x");
        RS232_PrintHex8(err.val);
        break;
      default: break;
    }
    RS232_Print("\n");
  }

  return err.errno;
}

// Only used for regularly scheduled periodic updates
AVCLAN_frame_t *AVCLAN_getStatusFrame() {
  static uint8_t status_data[] = STATUS_REPORT_DATA;
  static AVCLAN_frame_t status = {.broadcast = BROADCAST,
                                  .controller_addr = DEVICE_ADDR,
                                  .peripheral_addr = 0x1FF,
                                  .control = 0xF,
                                  .length = sizeof(status_data),
                                  .data = status_data};

  return &status;
}

// Used for changed status messages
void AVCLAN_generateStatus(AVCLAN_frame_t *status) {
  *status = (AVCLAN_frame_t){
      .broadcast = BROADCAST,
      .controller_addr = DEVICE_ADDR,
      .peripheral_addr = 0x1FF,
      .control = 0xF,
      .length = sizeof(cdstatus_resp),
      .data = status->data, // don't overwrite data pointer
  };
  status->data[0] = dev_CD_CHANGER;
  status->data[1] = dev_STATUS;
  status->data[2] = Status_Report;
  serializeCDStatus(&status->data[3]);
}

void AVCLAN_normalizeState() {
  // if (cd_status.state != cd_PLAYBACK) {
  cd_status.state = cd_PLAYBACK;
  cd_status.disk_scan = 0;
  cd_status.scan = 0;
  cd_status.flags2 = 0x80;
  // }
}

#ifdef SOFTWARE_DEBUG
uint16_t pulses[100];
uint16_t periods[100];

void AVCLan_Measure() {
  stopEvent();

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

  startEvent();
}
#endif
