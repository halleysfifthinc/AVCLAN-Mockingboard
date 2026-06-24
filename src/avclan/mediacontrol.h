// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Media-control: route/handle head-unit button presses to the audio source.

#pragma once

#include <stdint.h>

// Actions list
typedef enum : uint8_t {
  MEDIA_PLAY_PAUSE = 0,
  MEDIA_SKIP_FORWARD,
  MEDIA_SKIP_BACKWARD,
} AVCLAN_media_fn_t;

#ifdef __cplusplus
extern "C" {
#endif

// One-time hardware bring-up for the media driver.
void mediacontrol_init();

// Emulate a button press on the source device.
void AVCLAN_mediaFunction(AVCLAN_media_fn_t fn);

#ifndef NDEBUG
bool AVCLAN_micToggle();
bool AVCLAN_isMediaFunctioning();
#endif

#ifdef __cplusplus
}
#endif
