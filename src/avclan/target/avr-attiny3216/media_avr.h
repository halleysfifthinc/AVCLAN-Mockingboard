// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// AVR-internal media-driver hooks, shared between media_avr.c and the bus
// transaction guard (phy_avr.c's AVCLAN_stopEvent). Not part of the public
// mediacontrol.h interface.

#ifndef MEDIA_AVR_H
#define MEDIA_AVR_H

// Keep the TCA0 press waveform roughly in sync while a bus transaction has
// masked interrupts (runs the OVF ISR body early if an overflow is imminent).
// MUST be called with interrupts disabled (from within AVCLAN_stopEvent's
// ATOMIC_BLOCK).
void mediacontrol_syncDuringMask();

#endif // MEDIA_AVR_H
