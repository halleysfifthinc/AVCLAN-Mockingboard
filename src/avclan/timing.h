#ifndef _TIMING_HPP_
#define _TIMING_HPP_

// Physical AVC-LAN bit-phase durations, in microseconds. These are protocol
// facts (the bus spec), independent of any particular hardware. The active
// target provides TICK_US — the wall-clock duration, in microseconds, of one
// tick of whatever free-running timer it uses to measure/generate bus bits — so
// each constant below resolves to a count of that target's ticks.
//
// Kept as #defines (not constexpr): no target is guaranteed to want these as a
// specific integer width, so leave the type to the use site / target.

#ifndef TICK_US
  #error                                                                       \
      "target must define TICK_US (microseconds per bus-timer tick) before including timing.h"
#endif

// Measured at ±0.02 μs @ F_CPU=20MHz, TCB_CLKSEL=TCB_CLKSEL_CLKDIV1_gc
#define AVCLAN_STARTBIT_LOGIC_0 (169.0 / TICK_US)
#define AVCLAN_STARTBIT_LOGIC_1 (20.6 / TICK_US)

#define AVCLAN_BIT1_LOGIC_0 (19.7 / TICK_US)
#define AVCLAN_BIT1_LOGIC_1 (18.1 / TICK_US)

#define AVCLAN_BIT0_LOGIC_0 (32.85 / TICK_US)
#define AVCLAN_BIT0_LOGIC_1 (6.2 / TICK_US)

#define AVCLAN_READBIT_THRESHOLD (26.0 / TICK_US)

#define AVCLAN_BIT_LENGTH_MAX (39.1 / TICK_US)

#endif
