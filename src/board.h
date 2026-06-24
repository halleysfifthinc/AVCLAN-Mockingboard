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

// Board / MCU bring-up interface (the BSP seam). Implemented per-target (the AVR
// implementation is target/avr-attiny3216/board_avr.c). Keeps the app
// (sniffer.c) free of clock/pin/interrupt register access.

#ifndef BOARD_H
#define BOARD_H

// Clock setup + GPIO/pin configuration. Call once, first thing at startup
// (before any peripheral init).
void board_init(void);

// Globally enable interrupts. Call after all peripherals are initialized.
void board_interruptsEnable(void);

#endif // BOARD_H
