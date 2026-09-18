// copyright (C) 2006 Marcin Slonicki <marcin@softservice.com.pl>
// copyright (C) 2007 Louis Frigon
// Copyright (C) 2015 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/sfr_defs.h>
#include <stdint.h>
#include <util/atomic.h>

#include "avclan.h"
#include "hal/cd_timer.h" // statustimer_enable/disable (guard)
#include "hal/phy.h"
#include "media_avr.h" // media_sync_during_mask (guard)

// F_CPU + TICK_US (timing.h) defined here; F_CPU potentially needed by
// avr-libc.
#include "timing_avr.h" // IWYU pragma: keep

enum bit_t : uint8_t { bit_zero = 0x00, bit_one = 0x01, bit_start = 0x10 };
typedef enum bit_t Bit;

// USART0 TX ring indices owned by the jnk0le lib; the guard consults them to
// decide whether to resume the TX drain (DRE interrupt) on leave.
extern volatile uint8_t tx0_Head, tx0_Tail;

// Mask/unmask the USART interrupts during bit-banged AVC-LAN framing. TX is
// interrupt-driven, so the DRE (data-register-empty) interrupt is gated
// alongside RX; on re-enable, resume the TX drain only if bytes are still
// queued (enabling DREIE on an empty ring would transmit garbage).
static void console_set_irqs(bool enable) {
  if (enable) {
    USART0.CTRLA |= USART_RXCIE_bm;
    if (tx0_Head != tx0_Tail)
      USART0.CTRLA |= USART_DREIE_bm;
  } else {
    USART0.CTRLA &= ~(USART_RXCIE_bm | USART_DREIE_bm);
  }
}

// Name difference between avr-libc and Microchip pack
#if defined(EVSYS_ASYNCCH00_bm)
  #define EVSYS_ASYNCCH0_0_bm EVSYS_ASYNCCH00_bm
#endif

// AVC LAN bus on AC2 (PA6/7): PA6 AINP0 (+), PA7 AINN1 (-)
#define BUS_IS_IDLE (bit_is_clear(AC2_STATUS, AC_STATE_bp))

#define READING_BYTE   GPIOR1
#define READING_NBITS  GPIOR2
#define READING_PARITY GPIOR3

#define TCB_CNTMODE TCB_CNTMODE_PW_gc

static volatile uint16_t pulsewidth;

#ifndef NDEBUG
static volatile uint8_t pulse_count = 0;
static volatile uint16_t period = 0;
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

// Returns true if device TX is muted on the AVCLAN bus (both drive pins are
// configured as inputs).
bool phy_is_muted() {
  return (((VPORTA_DIR & PIN4_bm) | (VPORTA_DIR & PIN0_bm)) == 0);
}

// No-op: acks are driven from inside the synchronous read path, which a deaf
// caller never enters.
void phy_deafen([[maybe_unused]] bool deaf) {}

// True when the bus is being driven (i.e. not idle/floating).
bool phy_frame_pending() { return (!BUS_IS_IDLE) != 0; }

// Mute device TX on AVCLAN bus
void phy_mute(bool mute) {
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
}

static void phy_send_bit(Bit bit) {
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

static void phy_send_ack() {
  TCB1.CNT = 0;

  // Wait for controller to begin ACK bit
  while (BUS_IS_IDLE) {
    // Wait for approx the length of a bit; any longer and something has clearly
    // gone wrong
    if (TCB1.CNT >= AVCLAN_BIT_LENGTH_MAX)
      return;
  }

  phy_send_bit(bit_zero);
}

static Send phy_read_ack() {
  TCB1.CNT = 0;                              // Double reset of TCB1.CNT: here
  set_AVC_logic_for(0, AVCLAN_BIT1_LOGIC_0); // And here (within)
  AVCLAN_setBusIdle();                       // Stop driving bus

  while (true) {
    if (!BUS_IS_IDLE && (TCB1.CNT > AVCLAN_READBIT_THRESHOLD))
      break; // ACK
    if (TCB1.CNT > AVCLAN_BIT_LENGTH_MAX)
      return NAK;
  }

  // Check/wait in case we get here before peripheral finishes ACK bit
  while (!BUS_IS_IDLE) {
    if (TCB1.CNT > AVCLAN_BIT_LENGTH_MAX)
      return NAK_TOO_LONG;
  }
  return (Send)0;
}

// Send `len` bits on the AVCLAN bus; returns the even parity
static Bit phy_send_bits_u8(const uint8_t *bits, int8_t len) {
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
      Bit bit = (b & 0x80) != 0;
      parity += (uint8_t)bit;
      phy_send_bit(bit);
      b <<= 1;
    }
    len_mod8 = 8;
    // Avoid under-read for last byte
    if (len > 0)
      b = *--bits;
  }
  return (parity & 1);
}

// Send `len` bits on the AVCLAN bus; returns the even parity
static Bit phy_send_bits_u16(const uint16_t *bits, int8_t len) {
  return phy_send_bits_u8((const uint8_t *)bits + 1, len);
}

static Bit phy_send_byte(const uint8_t *byte) {
  uint8_t b = *byte;
  uint8_t parity = 0;

  for (uint8_t nbits = 8; nbits > 0; nbits--) {
    Bit bit = (b & 0x80) != 0;
    parity += (uint8_t)bit;
    phy_send_bit(bit);
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

  // If the logical `0` pulse was less than the sync + data period threshold,
  // bit was a 1
  uint16_t tmp = pulsewidth = TCB0.CCMP; // non-volatile tmp saves re-read in
                                         // read-bit-threshold comparison
  if (READING_NBITS) {
    READING_BYTE <<= 1;
    if (tmp < (uint16_t)AVCLAN_READBIT_THRESHOLD) {
      READING_BYTE++;
      READING_PARITY++;
    }
    READING_NBITS--;
  }
}

// Read `len` bits on the AVCLAN bus; returns the even parity
static Bit phy_read_bits_u8(uint8_t *bits, uint8_t len) {
  uint8_t parity;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    READING_BYTE = 0;
    READING_PARITY = 0;
    READING_NBITS = len;

    NONATOMIC_BLOCK(NONATOMIC_RESTORESTATE) {
      TCB1.CNT = 0;
      while (READING_NBITS) {
        // 200% the duration of `len` bits
        if (TCB1.CNT > ((uint16_t)AVCLAN_BIT_LENGTH_MAX * 2 * len)) {
          READING_BYTE = 0;
          READING_PARITY = 0;
          break; // Should have finished by now; something's wrong
        }
      };
    }

    *bits = READING_BYTE;
    parity = READING_PARITY;
  }

  return (Bit)(parity & 1);
}

// Read `len` bits on the AVCLAN bus; returns the even parity
static Bit phy_read_bits_u16(uint16_t *bits, int8_t len) {
  uint8_t parity = 0;
  if (len > 8) {
    uint8_t over = len - 8;
    parity = phy_read_bits_u8((uint8_t *)bits + 1, over);
    len -= over;
  }
  parity += phy_read_bits_u8((uint8_t *)bits + 0, len);

  return (Bit)(parity & 1);
}

// Read a byte on the AVCLAN bus
static Bit phy_read_byte(uint8_t *byte) {
  uint8_t parity;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    READING_BYTE = 0;
    READING_PARITY = 0;
    READING_NBITS = 8;

    NONATOMIC_BLOCK(NONATOMIC_RESTORESTATE) {
      TCB1.CNT = 0;
      while (READING_NBITS) {
        // 200% the length of a byte
        if (TCB1.CNT > ((uint16_t)AVCLAN_BIT_LENGTH_MAX * 2 * 8)) {
          READING_BYTE = 0;
          READING_PARITY = 0;
          break; // Should have finished by now; something's wrong
        }
      };
    }

    *byte = READING_BYTE;
    parity = READING_PARITY;
  }

  return (Bit)(parity & 1);
}

// Our own peripheral address, and whether the frame being read is addressed to
// it. hal/phy.h puts the ACK decision in the phy, so the latch that used to
// live in Bus::read lives here now: set when the peripheral address comes in,
// and reused by every field after it.
static uint16_t own_address_;
static bool acking_;

void phy_init(uint16_t address) {
  own_address_ = address;

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

  phy_mute(false); // unmute AVCLAN bus TX
}

static Read phy_read_startbit() {
  // Following HAL header docs, this function is only called after a driven bus
  // was ~recently detected.
  // Two main (designed) entry flows, depending on timing of pulse end w.r.t.
  // beginning this function:
  //  - Bus is still driven or ISR is pending
  //    - Wait for pulse to end if needed
  //      - On an over-long "driven" bus (AC2 latched high when bus is floating)
  //      kick PA7 hard high to try unlatching the comparator
  //    - Grab updated `pulsewidth`
  //  - Bus is idle
  //    - `pulsewidth` holds duration of most recent pulse.

  TCB1.CNT = 0;

  // "stale"ness indicates that the current `pulsewidth` value is out of date
  bool stale = true;

  uint16_t startbitlen;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    // pulsewidth won't/can't be updated during atomic block, but pulse may end
    // and store an IRQ. If bus is idle and no pending ISR, then pulsewidth
    // currrently holds the duration of the most recent pulse (stale = false).
    stale = (((!BUS_IS_IDLE) | (TCB0.INTFLAGS & TCB_CAPT_bm)) != 0);
    // Read ~atomically, to prevent torn reads
    startbitlen = pulsewidth;
  }

  while (!BUS_IS_IDLE) {
    if (TCB1.CNT > (uint16_t)AVCLAN_STARTBIT_LOGIC_0 * 1.2) {
      Read result = STARTBIT_TOO_LONG;
      while (!BUS_IS_IDLE) {
        // If bus is "driven" too long, assume the AC2 is latched (e.g.
        // because the bus is actually floating). Kick it if so.
        // This should prevent/resolve a flood of "STARTBIT_TOO_LONG" errors
        if (TCB1.CNT > (uint16_t)(AVCLAN_STARTBIT_LOGIC_0 * 3)) {
          result = BAD_STARTBIT;
          PORTA.OUTSET = PIN7_bm; // preset high before enabling the driver
          PORTA.DIRSET = PIN7_bm; // drive (-) hard high
          TCB1.CNT = 0;
          while (!BUS_IS_IDLE && TCB1.CNT < (uint16_t)AVCLAN_BIT0_LOGIC_1) {
            // Wait a max of ~6μs until bus is idle
          }
          PORTA.DIRCLR = PIN7_bm; // back to high-Z comparator input
          PORTA.OUTCLR = PIN7_bm;
        }
      }
      return result;
    }
  }

  TCB1.CNT = 0;
  while (stale) { // bus was driven, or a capture was still unconsumed, at entry
    uint16_t old_pulsewidth = startbitlen;
    // `pulsewidth` updates once the TCB0_INT_vect ISR runs for this pulse.
    // Read ~atomically, to prevent torn reads
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { startbitlen = pulsewidth; }
    if (startbitlen != old_pulsewidth)
      break;

    // Exit for potential false-negative: ISR writes for identical mid-frame
    // bits (e.g. double 0 or 1 bits) can't be detected, but the ISR should
    // definitely have run after ~6μs (i.e. startbitlen is actually current).
    // The delay is ultimately free since mid-frame entry is directed to the
    // "wait-for-message-completion" stall loop anyways
    if (TCB1.CNT > (uint16_t)AVCLAN_BIT0_LOGIC_1)
      break;
  }

  if (startbitlen < (uint16_t)(AVCLAN_STARTBIT_LOGIC_0 * 0.8)) {
    // Not a start bit; wait for the message to finish (bus continuously idle
    // for >1 bit length) before returning, so we only report one error (instead
    // of e.g. repeated "bad (short) start bit" errors)
    TCB1.CNT = 0;
    while (TCB1.CNT < (uint16_t)(AVCLAN_BIT_LENGTH_MAX * 1.2)) {
      if (!BUS_IS_IDLE)
        TCB1.CNT = 0; // (Re)start count from when bus was last driven
    }
    // A pulse no wider than a normal bit means we merely tuned in mid-frame and
    // this was a data bit; a wider-but-still-sub-start pulse means some other
    // device emitted a wonky pulse.
    return (startbitlen < (uint16_t)AVCLAN_BIT_LENGTH_MAX) ? STARTBIT_MISSED
                                                           : STARTBIT_MALFORMED;
  }
  if (startbitlen > (uint16_t)(AVCLAN_STARTBIT_LOGIC_0 * 1.2))
    return STARTBIT_TOO_LONG;
  return (Read)0; // that was a start bit
}

// Acquire the bus and emit a start bit. Returns false if another device is
// already driving the bus (we can't yet do proper CSMA/CD).
static Send phy_send_startbit() {
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
    // Can't yet simultaneously send and receive to do proper CSMA/CD

    // Beginnings of CSMA/CD
    // do {
    //   if (TCB1.CNT >= (uint16_t)(AVCLAN_STARTBIT_LOGIC_0 * 1.2))
    //     return false; // Something's hinky; nothing is longer than start bit
    // } while (!BUS_IS_IDLE);
    // if (TCB1.CNT <= (uint16_t)(AVCLAN_STARTBIT_LOGIC_0 * 0.8))
    //   return false; // Shouldn't be possible
    // set_AVC_logic_for(1, AVCLAN_STARTBIT_LOGIC_1); // wait for end of start
    return BUSY;
  }
  phy_send_bit(bit_start);
  return (Send)0;
}

// Field bits, then the wire's parity bit, checked against what we counted.
static Read read_parity(Bit calc, Read bad) {
  uint8_t wire = 0;
  phy_read_bits_u8(&wire, 1);
  return ((uint8_t)calc != wire) ? bad : (Read)0;
}

// An acknowledge slot follows the field whether or not we drive it, so a frame
// that isn't ours still has to have the slot read away.
static void ack_slot(void) {
  if (acking_) {
    phy_send_ack();
  } else {
    uint8_t slot = 0;
    phy_read_bits_u8(&slot, 1);
  }
}

Read phy_read_header(bool *is_unicast) {
  acking_ = false; // New frame; nothing is addressed to us yet

  const Read err = phy_read_startbit();
  if (err != (Read)0)
    return err;

  uint8_t bit = 0;
  phy_read_bits_u8(&bit, 1); // Broadcast bit: one bare bit, no parity
  *is_unicast = (bit != 0);
  return (Read)0;
}

Read phy_read_controller_addr(uint16_t *addr) {
  // No acknowledge slot: this field is the arbitration window.
  return read_parity(phy_read_bits_u16(addr, 12), BAD_CONTROLLER_PARITY);
}

Read phy_read_peripheral_addr(uint16_t *addr) {
  const Read err =
      read_parity(phy_read_bits_u16(addr, 12), BAD_PERIPHERAL_PARITY);
  if (err != (Read)0)
    return err;

  // The one field that decides the ack; every field after it reuses the answer.
  acking_ = !phy_is_muted() && (*addr == own_address_);
  ack_slot();
  return (Read)0;
}

Read phy_read_control(uint8_t *control) {
  const Read err =
      read_parity(phy_read_bits_u8(control, 4), BAD_CONTROL_PARITY);
  if (err != (Read)0)
    return err;
  ack_slot();
  return (Read)0;
}

Read phy_read_length(uint8_t *length) {
  const Read err = read_parity(phy_read_byte(length), BAD_LENGTH_PARITY);
  if (err != (Read)0)
    return err;
  ack_slot();
  return (Read)0;
}

Read phy_read_data(uint8_t *data) {
  const Read err = read_parity(phy_read_byte(data), BAD_DATA_PARITY);
  if (err != (Read)0)
    return err;
  ack_slot();
  return (Read)0;
}

// `expect_ack` is "a NAK here is worth reporting", not "emit the slot": the
// slot goes out either way, and a broadcast frame simply has nobody to fill it.
static Send send_ack_slot(bool expect_ack, Send nak) {
  if (expect_ack) {
    const Send err = phy_read_ack();
    return err == NAK ? nak : err;
  }

  const uint8_t fill = 1U;
  phy_send_bits_u8(&fill, 1);
  return (Send)0;
}

Send phy_send_header(bool is_unicast) {
  const Send err = phy_send_startbit();
  if (err != (Send)0)
    return err;

  const uint8_t bit = (is_unicast) ? 1U : 0U;
  phy_send_bits_u8(&bit, 1); // Broadcast bit: one bare bit, no parity
  return (Send)0;
}

Send phy_send_controller_addr(uint16_t addr) {
  // No acknowledge slot: this field is the arbitration window.
  phy_send_bit(phy_send_bits_u16(&addr, 12));
  return (Send)0;
}

Send phy_send_peripheral_addr(uint16_t addr, bool expect_ack) {
  phy_send_bit(phy_send_bits_u16(&addr, 12));
  return send_ack_slot(expect_ack, NAK_ADDRESS);
}

Send phy_send_control(uint8_t control, bool expect_ack) {
  phy_send_bit(phy_send_bits_u8(&control, 4));
  return send_ack_slot(expect_ack, NAK_CONTROL);
}

Send phy_send_length(uint8_t length, bool expect_ack) {
  phy_send_bit(phy_send_byte(&length));
  return send_ack_slot(expect_ack, NAK_MESSAGE_LENGTH);
}

Send phy_send_data(uint8_t data, bool expect_ack) {
  phy_send_bit(phy_send_byte(&data));
  return send_ack_slot(expect_ack, NAK_DATA);
}

// Every field above is on the wire, and has reported its own outcome, by the
// time it returns: nothing is left to wait out or attribute.
Send phy_send_done([[maybe_unused]] uint8_t *data_index) { return (Send)0; }

/* Disable non-read related interrupts (USART RX, RTC status tick, mic timer)
   during AVCLAN bus transactions so framing isn't disturbed. TCB0 must remain
   enabled. */
void phy_guard_enter() {
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    cdtimer_disable();
    console_set_irqs(false);
    media_sync_during_guard();
  }
}

// Re-enable serial and periodic interrupts after a bus transaction.
void phy_guard_leave() {
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    cdtimer_restore(); // Reenable status interrupt if currently playing
    console_set_irqs(true);
  }
}

#ifndef NDEBUG

// Hold the bus at a given level.
// Overrides and restores mute state upon release (i.e. set recessive).
static void phy_set_state(bool dominant) {
  static bool saved_mute = false;
  if (dominant) {
    saved_mute = phy_is_muted();
    AVCLAN_setBusDriven();
    phy_mute(false);
  } else {
    AVCLAN_setBusIdle();
    phy_mute(saved_mute);
  }
}

void phy_set_dominant(void) { phy_set_state(true); }
void phy_set_recessive(void) { phy_set_state(false); }

  #ifdef MEASURE_BUS
    #include <stdio.h> // phy_measure() reporting (debug builds only)

    // Only used immediately below
    #define XSTR(x) #x
    #define STR(x)  XSTR(x)

static uint16_t pulses[100];
static uint16_t periods[100];

void phy_measure() {
  phy_guard_enter();

  uint8_t tmp = 0;

  puts("Timing config: F_CPU=" STR(F_CPU) ", TCB_CLKSEL=" STR(TCB_CLKSEL));
  puts("Sampling bit (pulse-width and period) timing...");

  for (uint8_t n = 0; n < 100; n++) {
    while (pulse_count == tmp) {}
    pulses[n] = pulsewidth;
    periods[n] = period;
    tmp = pulse_count;
  }

  puts("Pulses:");
  for (uint8_t i = 0; i < 100; i++) {
    printf("%04X\n", pulses[i]);
  }

  puts("Periods:");
  for (uint8_t i = 0; i < 100; i++) {
    printf("%04X\n", periods[i]);
  }
  puts("\nDone.");

  phy_guard_leave();
}

  #endif
#endif
