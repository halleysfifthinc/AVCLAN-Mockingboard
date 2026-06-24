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
