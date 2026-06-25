// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// AVC-LAN message dispatcher and response state machine. Currently mixes
// generic peripheral-level handling with CD-changer device-specific handling;
// the device-specific cases will later migrate into cdchanger.

#pragma once

#include <stdint.h>

#include "avclan_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Message state machine
// - r_Nothing (0x00) means don't send current message
// - All other instances mean send current message and imply the presence of
//   follow-up messages within state machine
typedef enum : uint8_t {
  r_Nothing = 0x00,
  r_SendOnly,            // No further follow-up needed (beyond sending current)
  r_StatusReport = 0x02, // Needs follow-up status report
  r_NormalizeState,      // cd_status needs normalized and resent
  r_StartPlaying, // ~equivalent to normalizeState, but cycles to BeganPlaying
  r_BeganPlaying,
  r_TrackChange, // Time needs reset
  r_Ejection,
  r_Report_Load,
} reaction_t;

void AVCLAN_handleframe(const AVCLAN_frame_t *in, AVCLAN_frame_t *out);
void AVCLAN_statemachine(AVCLAN_frame_t *out);

#ifdef __cplusplus
}
#endif
