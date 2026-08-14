// Copyright (C) 2015 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// AVR ATtiny3216 timing parameters. Derives F_CPU (needed by avr-libc, e.g.
// util/delay.h) and the bus-timer (TCB) tick period from the CMake-provided
// FREQSEL / CLK_PRESCALE / TCB_CLKSEL, then hands the generic timing.h a
// TICK_US (microseconds per TCB tick) so the physical bit-phase durations
// resolve to TCB-tick counts. TICK_US == TCB_TICK / 1000, so every derived
// constant is numerically identical to the previous F_CPU/TCB_CLKSEL
// formulation.

#define __CLKCTRL_PDIV_2X_gc  2
#define __CLKCTRL_PDIV_4X_gc  4
#define __CLKCTRL_PDIV_8X_gc  8
#define __CLKCTRL_PDIV_16X_gc 16
#define __CLKCTRL_PDIV_32X_gc 32
#define __CLKCTRL_PDIV_64X_gc 64
#define __CLKCTRL_PDIV_6X_gc  6
#define __CLKCTRL_PDIV_10X_gc 10
#define __CLKCTRL_PDIV_12X_gc 12
#define __CLKCTRL_PDIV_24X_gc 24
#define __CLKCTRL_PDIV_48X_gc 48

#if CLK_PRESCALE == 0x01
  #define F_CPU     (FREQSEL / __CLK_PRESCALE_DIV)
  #define CYCLE_MUL __CLK_PRESCALE_DIV
#else
  #define F_CPU     (FREQSEL)
  #define CYCLE_MUL 1
#endif

// CPU_CYCLE / TCB_TICK are in nanoseconds.
#if FREQSEL == 20000000L
  #define CPU_CYCLE (50 * CYCLE_MUL)
#elif FREQSEL == 16000000L
  #define CPU_CYCLE (62.5 * CYCLE_MUL)
#else
  #error "Not implemented"
#endif

#ifndef TCB_CLKSEL_CLKDIV1_gc
  #define TCB_CLKSEL_CLKDIV1_gc (0x00 << 1)
#endif

#ifndef TCB_CLKSEL_CLKDIV2_gc
  #define TCB_CLKSEL_CLKDIV2_gc (0x01 << 1)
#endif

#ifndef TCB_CLKSEL_CLKTCA_gc
  #define TCB_CLKSEL_CLKTCA_gc (0x02 << 1)
#endif

#if TCB_CLKSEL == TCB_CLKSEL_CLKDIV1_gc
  #define TCB_TICK (CPU_CYCLE)
#elif TCB_CLKSEL == TCB_CLKSEL_CLKDIV2_gc
  #define TCB_TICK (CPU_CYCLE * 2)
#elif TCB_CLKSEL == TCB_CLKSEL_CLKTCA_gc
  #error "Not implemented"
#endif

// TCB_TICK is nanoseconds/tick; the generic timing.h wants microseconds/tick.
#define TICK_US (TCB_TICK / 1000.0)

#include "timing.h" // IWYU pragma: export
