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

// ~1 Hz status-update tick, driven by the RTC overflow. The overflow handler
// (ISR(RTC_CNT_vect)) lives in the app (sniffer.c); this module owns only the
// RTC hardware configuration and enable/disable/reset of the tick.

#ifndef STATUSTIMER_H
#define STATUSTIMER_H

// One-time RTC hardware bring-up (clock source + period). Leaves the overflow
// interrupt disabled.
void statustimer_init();

// Reset the count so the next tick is ~1 s out, and enable the overflow tick.
void statustimer_reset();

// Enable / disable the ~1 Hz overflow interrupt (bare register RMW; wrap in a
// critical section if called with interrupts enabled and concurrency matters).
void statustimer_enable();
void statustimer_disable();

#endif // STATUSTIMER_H
