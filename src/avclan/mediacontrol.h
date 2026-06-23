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

// Media-control actuator: emulates play/pause and skip button presses on the
// audio source by toggling MIC_CONTROL (PB1/WO1) with TCA0 in FRQ mode.

#ifndef MEDIACONTROL_H
#define MEDIACONTROL_H

#include <stdbool.h>

// One-time hardware bring-up for the mic/button-press driver (TCA0 + PB1).
void mediacontrol_init();

// Emulate a single play/pause button press on the source device.
void AVCLAN_micPlayPause();
// Emulate skip-forward/backward button presses
void AVCLAN_micSkipForward();
void AVCLAN_micSkipBackward();

// Keep the press waveform roughly in sync while a bus transaction has masked
// interrupts. MUST be called with interrupts disabled (e.g. from within the
// frame layer's AVCLAN_stopEvent ATOMIC_BLOCK).
void mediacontrol_syncDuringMask();

#ifndef NDEBUG
bool AVCLAN_micToggle();
bool AVCLAN_isMediaFunctioning();
#endif

#endif // MEDIACONTROL_H
