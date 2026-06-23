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

// AVC-LAN message dispatcher and response state machine. Currently mixes
// generic peripheral-level handling with CD-changer device-specific handling;
// the device-specific cases will later migrate into cdchanger.

#ifndef AVCLAN_PROTOCOL_H
#define AVCLAN_PROTOCOL_H

#include <stdint.h>

#include "avclan_defs.h"

/// Message state machine
// - r_Nothing (0x00) means don't send current message
// - r_Handled means send current message and stop/finished state machine
// - All other instances mean send current message and imply the presence of
//   follow-up messages within state machine
typedef enum : uint8_t {
  r_Nothing = 0x00,
  r_Handled,             // No follow-up needed
  r_StatusReport = 0x02, // Needs follow-up status report
  r_NormalizeState,      // cd_status needs normalized and resent
  r_StartPlaying, // ~equivalent to normalizeState, but cycles to BeganPlaying
  r_BeganPlaying,
  r_TrackChange, // Time needs reset
  r_Ejection,
  r_Report_Load,
} response_t;

typedef struct RFrame_struct {
  response_t r;
  AVCLAN_frame_t *frame;
} RFrame_t;

response_t AVCLAN_handleframe(const AVCLAN_frame_t *in, AVCLAN_frame_t *out);
RFrame_t *AVCLAN_statemachine(RFrame_t *resp);

#endif // AVCLAN_PROTOCOL_H
