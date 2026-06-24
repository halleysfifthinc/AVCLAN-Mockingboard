/*
                        AVCLAN-Mockingboard
    Copyright (C) 2015 Allen Hill <allenofthehills@gmail.com>

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
*/

#include <avr/interrupt.h>
#include <avr/io.h>
#include <stdint.h>
#include <util/atomic.h>

#include "media_avr.h"   // mediacontrol_syncDuringMask (used by the bus guard)
#include "mediacontrol.h"

// F_CPU defined in timing_avr.h; the mic tick constants below are derived from
// it (this hardware generation's TCA0/PB1 button-press implementation).
#include "timing_avr.h"

// pending WO1 toggles (even); signed to avoid underflows from a stray OVF
static volatile int8_t mic_ntoggles = 0;

// Target pulse length is ~40-150ms, with interval between pulses of
// ~100-200ms
// TCA0 period (CMP0/TOP) in ticks at F_CPU with the CLKSEL=DIV1024 prescaler.
// A press phase is ~100 ms; the final LOW phase is stretched to
// mic_refractory_period (~333 ms) so consecutive presses stay distinct
static constexpr uint16_t mic_press_ticks = (uint16_t)((F_CPU / 1024UL) / 10UL);
static constexpr uint16_t mic_refractory_period =
    (uint16_t)((F_CPU / 1024UL) / 3UL);

// the longest AVCLAN frame duration is ~15ms
static constexpr uint16_t close_thresh =
    (uint16_t)(mic_press_ticks * 15UL / 100UL);

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

// OVF ISR body function:
//  - counts phases
//  - stretches the final LOW phase as a refractory period, to keep separate
//    pulse trains distinct
//  - stops the timer after the last phase
static inline void mic_timer_isr_body(bool is_early) {
  if (--mic_ntoggles == 1) {
    // Stretch final phase to a ~333 ms idle-low so back-to-back presses stay
    // distinct
    // Invariant: mic_refractory_period > mic_press_ticks, so counter can never
    // silently wrap
    TCA0.SINGLE.CMP0 = mic_refractory_period;
  } else if (mic_ntoggles <= 0) {
    TCA0.SINGLE.CTRLA &= ~TCA_SINGLE_ENABLE_bm;
    TCA0.SINGLE.CTRLC = 0; // Timer must be disabled before (re)setting
                           // CTRLC/WO1 level (§20.5.3)
    mic_ntoggles = 0;      // clamp to avoid perma-lockout in mic_pulse
  }
  if (is_early)
    TCA0.SINGLE.CNT = 0;

  // OVF FLAG must be cleared via write. MUST BE PERFORMED LAST.
  // This function is called in two cases:
  //  - ISR, triggered by actual OVF: OVF FLAG is set and needs clearing
  //  - stopEvent, running ISR body ~early: OVF flag may be set while in
  //    ATOMIC_BLOCK (either in stopEvent, or earlier in this function body).
  //    Clear OVF FLAG as a precaution to prevent erroneous ISR runs
  TCA0.SINGLE.INTFLAGS = TCA_SINGLE_OVF_bm;
}

ISR(TCA0_OVF_vect) { mic_timer_isr_body(false); }

// Emulate a transport-control button press on the source device. Each action
// maps to a press-train of a given length on MIC_CONTROL.
void AVCLAN_mediaFunction(AVCLAN_media_fn_t fn) {
  switch (fn) {
    case MEDIA_PLAY_PAUSE: mic_pulse(1); break;    // single press
    case MEDIA_SKIP_FORWARD: mic_pulse(3); break;  // double-press
    case MEDIA_SKIP_BACKWARD: mic_pulse(5); break; // triple-press
  }
}

// Pre-emptively "overflow" and run the OVF ISR body early if a press is in
// progress and likely to overflow within the masked window. This maintains:
//  - a rough absolute time for the pulse train
//  - peripheral WO1 toggles and mic_ntoggles kept in sync
//  - maximum frame duration is ~15ms, "early" OVF remains within acceptable
//    ranges for either high/low pulses
// Caller (AVCLAN_stopEvent) guarantees interrupts are disabled.
void mediacontrol_syncDuringMask() {
  if (mic_ntoggles && TCA0.SINGLE.CNT >= (TCA0.SINGLE.CMP0 - close_thresh))
    mic_timer_isr_body(true);
}

void mediacontrol_init() {
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
  mic_ntoggles = 0;

  // toggle WO1 ~immediately after each period start; should go low => high
  TCA0.SINGLE.CMP1 = 2;
  TCA0.SINGLE.INTFLAGS = TCA_SINGLE_OVF_bm; // Clear OVF flag just in case
  TCA0.SINGLE.INTCTRL = 0;
}
