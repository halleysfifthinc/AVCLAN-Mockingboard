// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// CDChanger periodic-timer HAL: the ~1 Hz tick that drives the CD changer's
// status updates. The timer hardware is target-specific; the CD changer owns
// what a tick means (see cdchanger.cc). The device polls cdtimer_pending() and
// clears the tick with cdtimer_clear().

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// One-time hardware bring-up. Leaves the tick disabled.
void cdtimer_init(void *ptr, void(clbk)(void *), bool(isplay)(void *));

// Reset the count so the next tick is ~1 s out, and enable the tick.
void cdtimer_reset(void);

// Restore / disable the ~1 Hz tick.
void cdtimer_restore(void);
void cdtimer_disable(void);

extern volatile bool cdtimer_pending_flag;

static inline bool cdtimer_pending() { return cdtimer_pending_flag; }
static inline void cdtimer_clear() { cdtimer_pending_flag = false; }

#ifdef __cplusplus
}
#endif
