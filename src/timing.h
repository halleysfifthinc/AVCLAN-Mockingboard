#ifndef _TIMING_HPP_
#define _TIMING_HPP_

#define __CLKCTRL_PDIV_2X_gc 2
#define __CLKCTRL_PDIV_4X_gc 4
#define __CLKCTRL_PDIV_8X_gc 8
#define __CLKCTRL_PDIV_16X_gc 16
#define __CLKCTRL_PDIV_32X_gc 32
#define __CLKCTRL_PDIV_64X_gc 64
#define __CLKCTRL_PDIV_6X_gc 6
#define __CLKCTRL_PDIV_10X_gc 10
#define __CLKCTRL_PDIV_12X_gc 12
#define __CLKCTRL_PDIV_24X_gc 24
#define __CLKCTRL_PDIV_48X_gc 48

#if CLK_PRESCALE == 0x01
#define F_CPU (FREQSEL / __CLK_PRESCALE_DIV)
#define CYCLE_MUL __CLK_PRESCALE_DIV
#else
#define F_CPU (FREQSEL)
#define CYCLE_MUL 1
#endif

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

// Measured at ±0.02 μs @ F_CPU=20MHz, TCB_CLKSEL=TCB_CLKSEL_CLKDIV1_gc
#define AVCLAN_STARTBIT_LOGIC_0 (169e3 / TCB_TICK)
#define AVCLAN_STARTBIT_LOGIC_1 (20.6e3 / TCB_TICK)

#define AVCLAN_BIT1_LOGIC_0 (19.7e3 / TCB_TICK)
#define AVCLAN_BIT1_LOGIC_1 (18.1e3 / TCB_TICK)

#define AVCLAN_BIT0_LOGIC_0 (32.85e3 / TCB_TICK)
#define AVCLAN_BIT0_LOGIC_1 (6.2e3 / TCB_TICK)

#define AVCLAN_READBIT_THRESHOLD (26e3 / TCB_TICK)

#define AVCLAN_BIT_LENGTH_MAX (39.1e3 / TCB_TICK)

#endif
