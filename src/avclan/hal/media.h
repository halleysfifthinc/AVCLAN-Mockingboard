// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Media-control HAL: emulate head-unit button presses to the audio source. This
// is device functionality (see CDChanger), so this contract is consumed only by
// the device implementation, not the app.

#pragma once

#include "avclan.h"

#ifdef __cplusplus
using MediaAction = avclan::MediaAction;
extern "C" {
#else
typedef enum MediaAction MediaAction;
#endif

// One-time hardware bring-up for the media driver.
void media_init(void);

// Emulate a media function (button press) on the source device.
void media_action(MediaAction fn);

#ifndef NDEBUG
bool media_mic_toggle(void);
bool media_busy(void);
#endif

#ifdef __cplusplus
}
#endif
