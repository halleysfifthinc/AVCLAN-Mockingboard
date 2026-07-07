// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// AVR-internal media-driver hooks, shared between media_avr.c and the bus
// transaction guard (phy_avr.c's phy_guard_enter). Not part of the public
// mediacontrol.h interface.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Keep the TCA0 press waveform roughly in sync while a bus transaction has
// masked interrupts (runs the OVF ISR body early if an overflow is imminent).
// MUST be called with interrupts disabled (from within phy_guard_enter's
// ATOMIC_BLOCK).
void media_sync_during_guard();

#ifdef __cplusplus
}
#endif
