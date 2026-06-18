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
#include <util/atomic.h>

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

#ifndef NDEBUG
  #define TCB_CNTMODE TCB_CNTMODE_FRQPW_gc
#else
  #define TCB_CNTMODE TCB_CNTMODE_PW_gc
#endif

#define MAX_SEND_ATTEMPTS 3

static AVCLAN_CD_Status_t cd_status;

static uint8_t *cd_Track;
static uint8_t *cd_Time_Min;
static uint8_t *cd_Time_Sec;

static cd_modes CD_Mode;

#ifndef NDEBUG
static volatile uint8_t pulse_count = 0;
static volatile uint16_t period = 0;
#endif

static volatile uint16_t pulsewidth;

// answers
//
// 0xFF placeholders are variant bytes filled in by callers writing directly
// into out->data[N] after memcpy.
static const uint8_t lancheck_resp[] = {0x00, dev_COMM_CTRL, dev_LAN, 0xFF,
                                        0xFF};
static const uint8_t list_functions_resp[] = {
    0x00, dev_COMM_CTRL, dev_COMM_v1, List_Functions_Resp, dev_CD_CHANGER};
static const uint8_t ping_resp[] = {0x00,      dev_COMM_CTRL, dev_COMM_v1,
                                    Ping_Resp, 0xFF,          0x00};
static const uint8_t function_change_resp[] = {0x00, dev_CD_CHANGER,
                                               dev_COMM_v1, 0xFF, 0x01};

// No knowledge/understanding of field meaning/interpretation
static const uint8_t cdinitreport_resp[] = {
    dev_CD_CHANGER, dev_STATUS, Initial_Report_Response, 0x01, 0x31, 0x10,
    0x01,           0x01};

static const uint8_t cdloading_resp[] = {dev_CD_CHANGER,
                                         dev_STATUS,
                                         Loading_Status_Report,
                                         0x00,
                                         0x01,
                                         0x00,
                                         0x01,
                                         0x00,
                                         0x01,
                                         0x02};

// pending WO1 toggles (even); signed to avoid underflows from a stray OVF
static volatile int8_t mic_ntoggles = 0;

// TCA0 period (CMP0/TOP) in ticks at F_CPU with the CLKSEL=DIV1024 prescaler.
// A press phase is ~100 ms; the final LOW phase is stretched to mic_quiet_ticks
// (~500 ms) so consecutive presses stay distinct
static constexpr uint16_t mic_press_ticks = (uint16_t)((F_CPU / 1024UL) / 10UL);
static constexpr uint16_t mic_quiet_ticks = (uint16_t)((F_CPU / 1024UL) / 2UL);

#ifndef NDEBUG
// Toggle PB1 and return its new level.
bool AVCLAN_micToggle() {
  // Take manual control of PB1 (CMP1EN gives TCA0 control of WO1/PB1 level)
  TCA0.SINGLE.CTRLB &= ~TCA_SINGLE_CMP1EN_bm;
  VPORTB.OUT ^= PIN1_bm;
  return (VPORTB.OUT & PIN1_bm) != 0;
}

bool AVCLAN_isMediaFunctioning() { return mic_ntoggles != 0; }
#endif

// Begin a press waveform of `nphases` × 100 ms level segments.
// - ~Immediately toggles high, alternates each phase (1 = single HIGH press, 3
//   = skip H/L/H, etc).
// - Halting the timer freezes WO1 at its last level; must run even number of
// phases to ensure we return to low
static void mic_pulse(uint8_t nphases) {
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    if (mic_ntoggles) // Skip if already pulsing
      return;

    // must be even to return to idle-low
    mic_ntoggles = (nphases & 0x01) ? nphases + 1 : nphases;
    TCA0.SINGLE.CTRLB |=
        TCA_SINGLE_CMP1EN_bm; // Reassert TCA control of WO1/PB1
    TCA0.SINGLE.CTRLC = 0;    // Reset WO1 level just in case
    TCA0.SINGLE.CNT = 0;
    TCA0.SINGLE.CMP0 =   // TOP
        mic_press_ticks; // always restore default ~100 ms period
    TCA0.SINGLE.INTFLAGS = TCA_SINGLE_OVF_bm; // clear any stale flag
    TCA0.SINGLE.INTCTRL |= TCA_SINGLE_OVF_bm;
    TCA0.SINGLE.CTRLA |= TCA_SINGLE_ENABLE_bm;
  }
}

// OVF ISR counts phases, stretches the final LOW phase into a quiet gap, and
// stops the timer on the last one.
ISR(TCA0_OVF_vect) {
  TCA0.SINGLE.INTFLAGS = TCA_SINGLE_OVF_bm;
  if (--mic_ntoggles == 1) {
    // Stretch final phase to a ~500 ms idle-low so back-to-back presses stay
    // distinct
    TCA0.SINGLE.CMP0 = mic_quiet_ticks;
  } else if (mic_ntoggles <= 0) {
    mic_ntoggles = 0; // clamp to avoid perma-lockout in mic_pulse
    TCA0.SINGLE.CTRLA &= ~TCA_SINGLE_ENABLE_bm;
  }
}

// Emulate a single play/pause button press on the source device.
void AVCLAN_micPlayPause() { mic_pulse(1); }

// Emulate a skip-forward button press: H / L / H.
void AVCLAN_micSkip() { mic_pulse(3); }

/* Disable non-read related interrupts (USART RX, PIT, TCA) during AVCLAN reads.
 */
static inline void stopEvent() {
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    RTC.PITINTCTRL &= ~RTC_PI_bm;
    USART0.CTRLA &= ~USART_RXCIE_bm;

    // WO1 toggles don't depend on OVF interrupt, but the OVF interrupt *DOES*
    // count the toggles
    // So, disabling the OVF interrupt alone is insufficient, we must also
    // disable the timer
    TCA0.SINGLE.INTCTRL &= ~TCA_SINGLE_OVF_bm;

    // Target pulse length is ~40-150ms, with interval between pulses of
    // ~100-200ms
    // The longest AVCLAN frame duration is ~15ms, so stretching either phase
    // (high/low) won't exceed the allowable ranges for pulses (high) or
    // intervals (low)
    TCA0.SINGLE.CTRLA &= ~TCA_SINGLE_ENABLE_bm;
  }
}

// Re-enable serial and periodic interrupts.
static inline void startEvent() {
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    if (AVCLAN_isPlaying()) // Reenable PIT interrupt if currently playing
      RTC.PITINTCTRL |= RTC_PI_bm;
    USART0.CTRLA |= USART_RXCIE_bm;
    // Resume/re-arm mic-press timer only while a press is in progress.
    // Enable before unmasking so a pending final-phase OVF lands after
    // re-enable and the ISR's own ENABLE clear wins (no spurious extra period).
    if (mic_ntoggles) {
      TCA0.SINGLE.CTRLA |= TCA_SINGLE_ENABLE_bm;
      TCA0.SINGLE.INTCTRL |= TCA_SINGLE_OVF_bm;
    }
  }
}

// Sets CD_mode to play and resets timer count (so that the next interrupt is in
// 1 sec)
static void AVCLAN_startPlaying() {
  AVCLAN_micPlayPause();
  CD_Mode = stPlay;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    loop_until_bit_is_clear(RTC_PITSTATUS, RTC_CNTBUSY_bp);
    RTC.CNT = 0;
    RTC.PITINTCTRL |= RTC_PI_bm;
  }
}

// Sets CD_mode to play and resets timer count (so that the next interrupt is in
// 1 sec)
void AVCLAN_stopPlaying() {
  RTC.PITINTCTRL &= ~RTC_PI_bm;
  CD_Mode = stStop;
  AVCLAN_micPlayPause();
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
static inline bool AVCLAN_ismuted() {
  return (((VPORTA_DIR & PIN4_bm) | (VPORTA_DIR & PIN0_bm)) == 0);
}

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

  // PB1 needs to be set as an output for TCA0 to set the level
  PORTB.DIRSET = PIN1_bm;

  // Experimentally, a press should be ~100ms; multiple presses can be separated
  // by the same ~100ms (but separate pulse trains need more separation to
  // remain distinct)
  TCA0.SINGLE.CTRLA = TCA_SINGLE_CLKSEL_DIV1024_gc;

  // In frequency (FRQ) mode, channel N compare match triggers "UPDATE"
  // When CMPnEN is set, TCA0 has control of the output level for the channel's
  // pin, and UPDATE toggles the level
  // Channel 1 controls WO1, which is mapped to PB1
  TCA0.SINGLE.CTRLB = TCA_SINGLE_WGMODE_FRQ_gc | TCA_SINGLE_CMP1EN_bm;
  TCA0.SINGLE.CTRLC = 0; // Preset WO1 level low just to be sure

  // toggle WO1 ~immediately after each period start; should go low => high
  TCA0.SINGLE.CMP1 = 2;
  TCA0.SINGLE.INTFLAGS = TCA_SINGLE_OVF_bm; // Clear OVF flag just in case
  TCA0.SINGLE.INTCTRL = 0;

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
  RTC.PITINTCTRL = 0;
  loop_until_bit_is_clear(RTC_PITSTATUS, RTC_CTRLBUSY_bp);
  RTC.PITCTRLA = RTC_PERIOD_CYC32768_gc | RTC_PITEN_bm;

  AVCLAN_setBusIdle();

  AVCLAN_muteDevice(false); // unmute AVCLAN bus TX

  cd_status.cds = cd_CD1;
  cd_status.disc = 1;
  cd_status.state = cd_SEEKING | cd_SEEKING_TRACK;
  cd_status.flags = 0;
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

// Serialize cd_status into the wire format. The struct layout mirrors the wire
// format byte-for-byte, except for track/mins/secs, which need converted from
// decimal to BCD
static void serializeCDStatus(uint8_t *dst) {
  memcpy(dst, &cd_status, sizeof(cd_status));
  dst[3] = toBCD(cd_status.track);
  dst[4] = toBCD(cd_status.mins);
  dst[5] = toBCD(cd_status.secs);
}

bool AVCLAN_isPlaying() { return (CD_Mode == stPlay); }

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

static void AVCLAN_setTime(uint8_t mins, uint8_t secs) {
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
#ifndef NDEBUG
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
      // hang until bus is idle to avoid repeated STARTBIT_TOO_LONG
      // errors when the AC is stuck (observed when cycling car power and
      // mockingboard is externally powered by serial/updi)
      while (!BUS_IS_IDLE) {}
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

  AVCLAN_readbits(&tmp, 1);
  frame->is_unicast = tmp;

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

  bool shouldACK = !AVCLAN_ismuted() && (frame->peripheral_addr == DEVICE_ADDR);

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

  if (false) {
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

  // Only print if some data has been correctly received
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
  AVCLAN_sendbits(&(uint8_t){frame->is_unicast}, 1);

  avclan_bit_t parity = AVCLAN_sendbits(&frame->controller_addr, 12);
  AVCLAN_sendbit(parity);

  parity = AVCLAN_sendbits(&frame->peripheral_addr, 12);
  AVCLAN_sendbit(parity);

  if (frame->is_unicast && !AVCLAN_readbit_ACK()) {
    err.errno = NAK_ADDRESS;
    goto handle_err;
  }

  parity = AVCLAN_sendbits(&frame->control, 4);
  AVCLAN_sendbit(parity);

  if (frame->is_unicast && !AVCLAN_readbit_ACK()) {
    err.errno = NAK_CONTROL;
    goto handle_err;
  }

  parity = AVCLAN_sendbyte(&frame->length); // data length
  AVCLAN_sendbit(parity);

  if (frame->is_unicast && !AVCLAN_readbit_ACK()) {
    err.errno = NAK_MESSAGE_LENGTH;
    goto handle_err;
  }

  for (uint8_t i = 0; i < frame->length; i++) {
    parity = AVCLAN_sendbyte(&frame->data[i]);
    AVCLAN_sendbit(parity);
    // Based on the µPD6708 datasheet, ACK bit for broadcast doesn't seem
    // necessary (i.e. This deviates from the previous broadcast specific
    // function that sent an extra `1` bit after each byte/parity)
    if (frame->is_unicast && !AVCLAN_readbit_ACK()) {
      err.errno = NAK_DATA;
      err.val = i;
      goto handle_err;
    }
    // else
    //   AVCLAN_sendbit_1();
  }

  // back to read mode
  if (false) {
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

#define PACK3(a, b, c) (((uint32_t)(a) << 16) | ((uint32_t)(b) << 8) | (c))

response_t AVCLAN_handleframe(const AVCLAN_frame_t *in, AVCLAN_frame_t *out) {
  response_t respond = r_Nothing;

  if (AVCLAN_ismuted() || in->length < 3)
    return respond;

  out->controller_addr = DEVICE_ADDR;
  out->control = 0xF;

  const uint8_t *data = in->data;
  const uint8_t b0 = *data++;
  const uint8_t b1 = *data++;
  const uint8_t b2 = *data++;
  uint8_t b3 = 0;
  if (in->length > 3) // the shortest known/valid messages are 3 bytes long
    b3 = *data++;

  if (!in->is_unicast) {
    // Broadcast: bytes are (from, to, action, [extra...]).
    // peripheral_addr unchecked — always 0xFFF or 0x1FF in known traffic.
    switch (PACK3(b0, b1, b2)) {
      case PACK3(dev_LAN, dev_COMM_CTRL, Lancheck_Scan_Req):
        out->length = sizeof(lancheck_resp);
        out->is_unicast = true;
        out->peripheral_addr = HU_ADDR;
        memcpy(out->data, lancheck_resp, sizeof(lancheck_resp));
        out->data[3] = Lancheck_Scan_Resp;
        out->data[4] = 0x01;
        respond = r_Handled;
        break;
      case PACK3(dev_LAN, dev_COMM_CTRL, Lancheck_Req):
        out->length = sizeof(lancheck_resp);
        out->is_unicast = true;
        out->peripheral_addr = HU_ADDR;
        memcpy(out->data, lancheck_resp, sizeof(lancheck_resp));
        out->data[3] = Lancheck_Resp;
        out->data[4] = 0x00;
        respond = r_Handled;
        break;
      case PACK3(dev_LAN, dev_COMM_CTRL, Lancheck_End_Req):
        out->is_unicast = true;
        out->peripheral_addr = HU_ADDR;
        out->length = sizeof(lancheck_resp) - 1;
        memcpy(out->data, lancheck_resp, out->length);
        out->data[3] = Lancheck_End_Resp;
        respond = r_Handled;
        break;
      case PACK3(dev_COMM_v1, dev_COMM_CTRL, Current_Function):
      case PACK3(dev_COMM_v2, dev_COMM_CTRL, Current_Function):
        if ((b3 == dev_CD_CHANGER) && !AVCLAN_isPlaying()) {
          if (cd_status.mins > 99)
            cd_status.mins = 0;
          if (cd_status.secs > 99)
            cd_status.secs = 0;
          cd_status.state = cd_SEEKING | cd_SEEKING_TRACK;
          cd_status.flags2 = 0xc0;
          AVCLAN_generateStatus(out, true, dev_STATUS);
          AVCLAN_startPlaying();
          respond = r_NormalizeState;
        }
        break;
      case PACK3(dev_COMM_v1, dev_COMM_CTRL, Ping_Req):
      case PACK3(dev_COMM_v2, dev_COMM_CTRL, Ping_Req):
        out->is_unicast = true;
        out->peripheral_addr = HU_ADDR;
        out->length = sizeof(ping_resp);
        memcpy(out->data, ping_resp, sizeof(ping_resp));
        out->data[4] = b3;
        respond = r_Handled;
        break;
      case PACK3(dev_COMM_v1, dev_COMM_CTRL, List_Functions_Req):
      case PACK3(dev_COMM_v2, dev_COMM_CTRL, List_Functions_Req):
        out->is_unicast = true;
        out->peripheral_addr = HU_ADDR;
        out->length = sizeof(list_functions_resp);
        memcpy(out->data, list_functions_resp, sizeof(list_functions_resp));
        respond = r_Handled;
        break;
        // case Restart_Lan: not handled
    }
  } else if (in->peripheral_addr == DEVICE_ADDR && b0 == 0x00) {
    // Unicast to CD changer: bytes are (0x00, from, to, action, [extra...]).
    switch (PACK3(b1, b2, b3)) {
      case PACK3(dev_COMM_v1, dev_CD_CHANGER, Enable_Function_Req):
        [[fallthrough]];
      case PACK3(dev_COMM_v2, dev_CD_CHANGER, Enable_Function_Req):
        out->is_unicast = true;
        out->peripheral_addr = HU_ADDR;
        out->length = sizeof(function_change_resp);
        memcpy(out->data, function_change_resp, sizeof(function_change_resp));
        out->data[3] = Enable_Function_Resp;
        cd_status.state = 0;
        cd_status.flags2 = 0x80;
        respond = r_StatusReport;
        break;
      case PACK3(dev_COMM_v1, dev_CD_CHANGER, Disable_Function_Req):
        [[fallthrough]];
      case PACK3(dev_COMM_v2, dev_CD_CHANGER, Disable_Function_Req):
        AVCLAN_stopPlaying();
        out->length = sizeof(function_change_resp);
        memcpy(out->data, function_change_resp, sizeof(function_change_resp));
        out->data[3] = Disable_Function_Resp;
        cd_status.state = cd_PLAYBACK | cd_SEEKING_TRACK;
        cd_status.flags2 = 0x80;
        out->is_unicast = true;
        out->peripheral_addr = HU_ADDR;
        respond = r_StatusReport;
        break;
      case PACK3(dev_CMD_SW, dev_CD_CHANGER, Eject): {
        // "Eject" label is multiply wrong; proper meaning unclear:
        //    - First observed on initial multiple presses of "CD" button,
        //    triggering (after {0x00, dev_CD_CHANGER, dev_COMM_v1, Insertion,
        //    0x01} response) proper activation of mockingboard/cd-changer.
        //    - Subsequently observed when pressing (technically
        //    releasing?) the fast-forward button and rewind
        if (cd_status.state | cd_SEEKING) { // FF/RW button released
          cd_status.state &= ~cd_SEEKING;
        } else {
          out->is_unicast = true;
          out->peripheral_addr = HU_ADDR;
          {
            const uint8_t msg[] = {0x00, dev_CD_CHANGER, dev_CMD_SW, Insertion,
                                   0x01};
            out->length = sizeof(msg);
            memcpy(out->data, msg, sizeof(msg));
          }
          respond = r_Handled;
        }
        break;
      }
      case PACK3(dev_CMD_SW, dev_CD_CHANGER, Initial_Report_Request):
        [[fallthrough]];
      case PACK3(dev_STATUS, dev_CD_CHANGER, Initial_Report_Request):
        out->data[0] = 0x00; // Add leading zero-byte for unicast comms
        out->length = sizeof(cdinitreport_resp) + 1;
        memcpy(&out->data[1], cdinitreport_resp, sizeof(cdinitreport_resp));
        out->data[2] = b1; // respond to device that requested
        out->is_unicast = true;
        out->peripheral_addr = HU_ADDR;
        respond = r_Handled;
        break;
      case PACK3(dev_CMD_SW, dev_CD_CHANGER, Playback_Request): [[fallthrough]];
      case PACK3(dev_STATUS, dev_CD_CHANGER, Playback_Request):
        out->data[0] = 0x00;
        out->data[1] = dev_CD_CHANGER;
        out->data[2] = b1;
        out->data[3] = Playback_Report;
        out->length = sizeof(AVCLAN_CD_Status_t) + 4;
        serializeCDStatus(&out->data[4]);
        out->is_unicast = true;
        out->peripheral_addr = HU_ADDR;
        respond = r_Handled;
        break;
      case PACK3(dev_CMD_SW, dev_CD_CHANGER, Loading_Request2): [[fallthrough]];
      case PACK3(dev_STATUS, dev_CD_CHANGER, Loading_Request2):
        out->data[0] = 0x00;
        out->length = sizeof(cdloading_resp) + 1;
        memcpy(&out->data[1], cdloading_resp, sizeof(cdloading_resp));
        out->data[2] = b1;
        out->data[3] = Loading_Response2;
        out->is_unicast = true;
        out->peripheral_addr = HU_ADDR;
        respond = r_Handled;
        break;
      case PACK3(dev_CMD_SW, dev_CD_CHANGER, Track_Seek_Up):
        AVCLAN_micSkip();
        cd_status.state = cd_SEEKING_TRACK;
        if (*cd_Track < 98)
          ++*cd_Track;
        else
          *cd_Track = 1;
        *cd_Time_Min = 0xff;
        *cd_Time_Sec = 0x7f;
        cd_status.flags2 = 0xc0;
        AVCLAN_generateStatus(out, true, dev_CMD_SW);
        respond = r_TrackChange;
        break;
      case PACK3(dev_CMD_SW, dev_CD_CHANGER, Track_Seek_Down):
        cd_status.state = cd_SEEKING_TRACK;
        // Track down returns to track beginning if in ~middle of song
        if (*cd_Time_Min == 0 && *cd_Time_Sec < 0x05) {
          if (*cd_Track > 1)
            --*cd_Track;
          else
            *cd_Track = 99;
        }
        *cd_Time_Min = 0xff;
        *cd_Time_Sec = 0x7f;
        cd_status.flags2 = 0xc0;
        AVCLAN_generateStatus(out, true, dev_CMD_SW);
        respond = r_TrackChange;
        break;
      case PACK3(dev_CMD_SW, dev_CD_CHANGER, Track_Fast_Forward): {
        cd_status.state |= cd_SEEKING;
        *cd_Time_Sec += 15;
        if (*cd_Time_Sec > 60) {
          *cd_Time_Sec -= 60;
          ++*cd_Time_Min;
        }
        AVCLAN_generateStatus(out, true, dev_CMD_SW);
        respond = r_Handled;
        break;
      }
      case PACK3(dev_CMD_SW, dev_CD_CHANGER, Track_Rewind): {
        cd_status.state |= cd_SEEKING;
        if (*cd_Time_Sec < 15) {
          if (*cd_Time_Min > 0) {
            uint8_t d = 15 - *cd_Time_Sec;
            *cd_Time_Sec = 60 - d;
            --*cd_Time_Min;
          } else {
            *cd_Time_Min = 0;
            *cd_Time_Sec = 0;
          }
        } else
          *cd_Time_Sec -= 15;
        AVCLAN_generateStatus(out, true, dev_CMD_SW);
        respond = r_Handled;
        break;
      }
      case PACK3(dev_CMD_SW, dev_CD_CHANGER, CD_Enable_Random):
        cd_status.flags |= cd_RANDOM;
        AVCLAN_generateStatus(out, true, dev_CMD_SW);
        respond = r_StatusReport;
        break;
      case PACK3(dev_CMD_SW, dev_CD_CHANGER, CD_Disable_Random):
        cd_status.flags &= ~cd_RANDOM;
        AVCLAN_generateStatus(out, true, dev_CMD_SW);
        respond = r_StatusReport;
        break;
      case PACK3(dev_CMD_SW, dev_CD_CHANGER, CD_Enable_Repeat):
        cd_status.flags |= cd_REPEAT;
        AVCLAN_generateStatus(out, true, dev_CMD_SW);
        respond = r_StatusReport;
        break;
      case PACK3(dev_CMD_SW, dev_CD_CHANGER, CD_Disable_Repeat):
        cd_status.flags &= ~cd_REPEAT;
        AVCLAN_generateStatus(out, true, dev_CMD_SW);
        respond = r_StatusReport;
        break;
      case PACK3(dev_CMD_SW, dev_CD_CHANGER, CD_Enable_Disk_Random):
        cd_status.flags |= cd_DISK_RANDOM;
        AVCLAN_generateStatus(out, true, dev_CMD_SW);
        respond = r_StatusReport;
        break;
      case PACK3(dev_CMD_SW, dev_CD_CHANGER, CD_Disable_Disk_Random):
        cd_status.flags &= ~cd_DISK_RANDOM;
        AVCLAN_generateStatus(out, true, dev_CMD_SW);
        respond = r_StatusReport;
        break;
      case PACK3(dev_CMD_SW, dev_CD_CHANGER, CD_Enable_Disk_Repeat):
        cd_status.flags |= cd_DISK_REPEAT;
        AVCLAN_generateStatus(out, true, dev_CMD_SW);
        respond = r_StatusReport;
        break;
      case PACK3(dev_CMD_SW, dev_CD_CHANGER, CD_Disable_Disk_Repeat):
        cd_status.flags &= ~cd_DISK_REPEAT;
        AVCLAN_generateStatus(out, true, dev_CMD_SW);
        respond = r_StatusReport;
        break;
    }
  }

  return respond;
}

#undef PACK3

RFrame_t *AVCLAN_statemachine(RFrame_t *resp) {
  AVCLAN_frame_t *out = resp->frame;
  switch (resp->r) {
    case r_Ejection: {
      const uint8_t play[] = {0x00,      dev_COMM_CTRL,  dev_COMM_v1,
                              Insertion, dev_CD_CHANGER, 0x01};
      out->length = sizeof(play);
      memcpy(out->data, play, sizeof(play));
    }
      resp->r = r_Report_Load;
      break;
    case r_Report_Load:
      out->is_unicast = false;
      out->peripheral_addr = 0x1FF;
      out->length = sizeof(cdloading_resp) + 1;
      memcpy(out->data, cdloading_resp, sizeof(cdloading_resp));
      out->data[1] = dev_STATUS;
      out->data[2] = Loading_Status_Report;
      resp->r = r_Handled;
      break;
    case r_TrackChange: AVCLAN_setTime(0x00, 0x00); [[fallthrough]];
    case r_NormalizeState:
      AVCLAN_normalizeState();
      AVCLAN_generateStatus(out, true, dev_STATUS);
      resp->r = r_Handled;
      break;
    case r_StartPlaying:
      AVCLAN_generateStatus(out, true, dev_STATUS);
      resp->r = r_NormalizeState;
      break;
    case r_StatusReport:
      AVCLAN_generateStatus(out, true, dev_STATUS);
      resp->r = r_Handled;
      break;
    case r_Handled: [[fallthrough]];
    case r_Nothing: [[fallthrough]];
    default: resp->r = r_Nothing;
  }
  return resp;
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

void AVCLAN_printframe(const AVCLAN_frame_t *frame, bool binary) {
  if (binary) {
    uint8_t buffer[8];
    buffer[0] = 0x10; // Data Link Escape, signaling binary data forthcoming
    buffer[1] = frame->is_unicast;

    // Send addresses in big-endian order
    buffer[2] = (uint8_t)(frame->controller_addr >> 8);
    buffer[3] = (uint8_t)frame->controller_addr;
    buffer[4] = (uint8_t)(frame->peripheral_addr >> 8);
    buffer[5] = (uint8_t)frame->peripheral_addr;

    buffer[6] = frame->control;
    buffer[7] = frame->length;
    RS232_sendbytes((uint8_t *)&buffer, 8);
    RS232_sendbytes(frame->data, frame->length);

    buffer[0] = 0x17; // End of transmission block
    buffer[1] = 0x0D; // \r
    buffer[2] = 0x0A; // \n
    RS232_sendbytes((uint8_t *)&buffer, 3);
  } else {
    RS232_PrintHex4(frame->is_unicast);

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

  frame->is_unicast = *bytes++;
  frame->controller_addr = bytes[0] | ((uint16_t)bytes[1] << 8);
  bytes += 2;
  frame->peripheral_addr = bytes[0] | ((uint16_t)bytes[1] << 8);
  bytes += 2;
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

  if (false) {
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
  static uint8_t status_data[sizeof(AVCLAN_CD_Status_t) + 3] = {0};
  static AVCLAN_frame_t status = {.is_unicast = false,
                                  .controller_addr = DEVICE_ADDR,
                                  .peripheral_addr = 0x1FF,
                                  .control = 0xF,
                                  .length = sizeof(status_data),
                                  .data = status_data};

  return &status;
}

// Used for changed status messages
void AVCLAN_generateStatus(AVCLAN_frame_t *status, bool is_unicast,
                           devices to) {
  *status = (AVCLAN_frame_t){
      .is_unicast = is_unicast,
      .controller_addr = DEVICE_ADDR,
      .peripheral_addr = (is_unicast) ? HU_ADDR : 0x1FF,
      .control = 0xF,
      .length = sizeof(AVCLAN_CD_Status_t) + ((is_unicast) ? 4 : 3),
      .data = status->data, // don't overwrite data pointer
  };

  uint8_t *data = status->data;
  if (is_unicast)
    *data++ = 0x00;
  *data++ = dev_CD_CHANGER;
  *data++ = to;
  *data++ = Status_Report;
  serializeCDStatus(data);
}

void AVCLAN_normalizeState() {
  // if (cd_status.state != cd_PLAYBACK) {
  if (cd_status.mins > 99)
    cd_status.mins = 0;
  if (cd_status.secs > 99)
    cd_status.secs = 0;
  cd_status.state = cd_PLAYBACK;
  cd_status.flags &= (uint8_t)~(cd_DISK_SCAN | cd_SCAN);
  cd_status.flags2 = 0x80;
  // }
}

#ifndef NDEBUG
  // Only used immediately below
  #define XSTR(x) #x
  #define STR(x)  XSTR(x)

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
    RS232_PrintHex8((uint8_t)(pulses[i] >> 8));
    RS232_PrintHex8((uint8_t)pulses[i]);
    RS232_Print("\n");
  }

  RS232_Print("Periods:\n");
  for (uint8_t i = 0; i < 100; i++) {
    RS232_PrintHex8((uint8_t)(periods[i] >> 8));
    RS232_PrintHex8((uint8_t)periods[i]);
    RS232_Print("\n");
  }
  RS232_Print("\nDone.\n");

  startEvent();
}
#endif
