// copyright (C) 2006 Marcin Slonicki <marcin@softservice.com.pl>
// copyright (C) 2007 Louis Frigon
// Copyright (C) 2015 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <avr/interrupt.h>
#include <avr/io.h>
#include <stdint.h>
#include <util/atomic.h>

#include "cdchanger.h"
#include "statustimer.h"

// Measured wall-clock duration (in ms) of one nominal 32768-tick RTC period,
// used to calibrate out the internal OSCULP32K's error. The RTC runs from
// OSCULP32K, which is only spec'd to +/-3% and has no user calibration
// register, so a nominal 32768-count period does not land on exactly 1 s.
// Override per-board via the CMake cache (see CMakeUserPresets.json).
#ifndef RTC_STATUS_PERIOD_MS
  #define RTC_STATUS_PERIOD_MS 1000
#endif

// RTC overflow period (in 32.768 kHz ticks) for the ~1 Hz status-update tick.
// ticks = round(32768 * 1000 / RTC_STATUS_PERIOD_MS); the RTC overflows after
// PER+1 ticks, so PER = ticks - 1.
static constexpr uint16_t rtc_status_per =
    (uint16_t)(32768UL * 1000UL / RTC_STATUS_PERIOD_MS) - 1U;

void statustimer_init() {
  // Setup RTC as a ~1 sec periodic timer via the normal counter's overflow.
  // Use the RTC directly (not PIT) to tune the status report interval closer to
  // 1 sec (internal osc may be slightly off)
  loop_until_bit_is_clear(RTC_STATUS, RTC_CTRLABUSY_bp);
  RTC.CLKSEL = RTC_CLKSEL_INT32K_gc;
  loop_until_bit_is_clear(RTC_STATUS, RTC_PERBUSY_bp);
  RTC.PER = rtc_status_per;
  RTC.INTCTRL = 0;
  loop_until_bit_is_clear(RTC_STATUS, RTC_CTRLABUSY_bp);
  RTC.CTRLA = RTC_PRESCALER_DIV1_gc | RTC_RTCEN_bm;
}

void statustimer_reset() {
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    loop_until_bit_is_clear(RTC_STATUS, RTC_CNTBUSY_bp);
    RTC.CNT = 0;
    RTC.INTFLAGS = RTC_OVF_bm; // Clear interrupt flag just in case
    RTC.INTCTRL |= RTC_OVF_bm;
  }
}

void statustimer_enable() { RTC.INTCTRL |= RTC_OVF_bm; }

void statustimer_disable() { RTC.INTCTRL &= ~RTC_OVF_bm; }

// Set once per overflow; consumed by the app via statustimer_tickPending().
volatile bool tick_pending = false;

// Periodic interrupt with a ~1 sec period; only enabled while playing.
ISR(RTC_CNT_vect) {
  AVCLAN_incrementTime();
  tick_pending = true;
  RTC.INTFLAGS = RTC_OVF_bm;
}
