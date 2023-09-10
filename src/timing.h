#ifndef _TIMING_HPP_
#define _TIMING_HPP_

#if CLK_PRESCALE == 0x01
#error "Not implemented"
#else

#if F_CPU == 20000000L
#define CPU_CYCLE 50
#elif F_CPU == 16000000L
#define CPU_CYCLE 62.5
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

#define AVCLAN_STARTBIT_LOGIC_0 (166e3 / TCB_TICK)
#define AVCLAN_STARTBIT_LOGIC_1 (19e3 / TCB_TICK)

#define AVCLAN_BIT1_LOGIC_0 (20.5e3 / TCB_TICK)
#define AVCLAN_BIT1_LOGIC_1 (19e3 / TCB_TICK)

#define AVCLAN_BIT0_LOGIC_0 (34e3 / TCB_TICK)
#define AVCLAN_BIT0_LOGIC_1 (5.5e3 / TCB_TICK)

#define AVCLAN_READBIT_THRESHOLD (26e3 / TCB_TICK)

#define AVCLAN_BIT_LENGTH (39.5e3 / TCB_TICK)

#endif

#endif