// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// ~1 Hz status-update tick interface. The app
// polls statustimer_tickPending() and clears the tick with
// statustimer_clearTick()

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif
