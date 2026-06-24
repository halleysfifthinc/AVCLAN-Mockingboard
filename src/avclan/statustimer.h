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

// ~1 Hz status-update tick interface. The app
// polls statustimer_tickPending() and clears the tick with
// statustimer_clearTick()

#ifndef STATUSTIMER_H
#define STATUSTIMER_H

// One-time hardware bring-up. Leaves the tick disabled.
void statustimer_init(void);

// Reset the count so the next tick is ~1 s out, and enable the tick.
void statustimer_reset(void);

// Enable / disable the ~1 Hz tick.
void statustimer_enable(void);
void statustimer_disable(void);

extern volatile bool tick_pending;

static inline bool statustimer_tickPending() { return tick_pending; }
static inline void statustimer_clearTick() { tick_pending = false; }

#endif // STATUSTIMER_H
