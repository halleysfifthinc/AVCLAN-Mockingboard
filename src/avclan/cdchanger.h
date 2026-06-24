// copyright (C) 2006 Marcin Slonicki <marcin@softservice.com.pl>
// copyright (C) 2007 Louis Frigon
// Copyright (C) 2015 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Emulated CD-changer device (AVC-LAN device 0x63): playback state, BCD time,
// status-frame generation, and the play/stop mode FSM. This is pure-ish device
// logic; it commands the mediacontrol and statustimer drivers rather than
// touching their hardware directly.

#pragma once

#include <stdint.h>

#include "avclan_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum : uint8_t {
  cd_OPEN = 0x01,
  cd_ERR1 = 0x02,
  cd_SEEKING = 0x08,
  cd_PLAYBACK = 0x10,
  cd_SEEKING_TRACK = 0x20,
  cd_LOADING = 0x80,
} cd_state;

typedef enum : uint8_t {
  cd_CD1 = 1 << 0,
  cd_CD2 = 1 << 1,
  cd_CD3 = 1 << 2,
  cd_CD4 = 1 << 3,
  cd_CD5 = 1 << 4,
  cd_CD6 = 1 << 5,
} cd_present_t;

typedef enum : uint8_t {
  cd_DISK_RANDOM = 1 << 1,
  cd_RANDOM = 1 << 2,
  cd_DISK_REPEAT = 1 << 3,
  cd_REPEAT = 1 << 4,
  cd_DISK_SCAN = 1 << 5,
  cd_SCAN = 1 << 6,
} cd_flag_t;

typedef struct AVCLAN_CD_Status {
  uint8_t cds;
  uint8_t state;
  uint8_t disc;
  uint8_t track; // Decimal storage; serialize to BCD
  uint8_t mins;  // Decimal storage; serialize to BCD
  uint8_t secs;  // Decimal storage; serialize to BCD
  uint8_t flags;
  uint8_t flags2;
} AVCLAN_CD_Status_t;

typedef enum : uint8_t { stStop = 0, stPlay = 1 } cd_modes;

// Mutable device state. Exposed for the protocol dispatcher, which currently
// mutates these fields directly. (Will be re-encapsulated when the device-
// specific message handling migrates here from avclan_protocol.)
extern AVCLAN_CD_Status_t cd_status;

// Full device-stack bring-up (calls AVCLAN_phyInit / mediacontrol_init /
// statustimer_init, then initialises cd_status).
void AVCLAN_init();

bool AVCLAN_isPlaying();
void AVCLAN_startPlaying();
void AVCLAN_stopPlaying();
void AVCLAN_incrementTime();
void AVCLAN_setTime(uint8_t mins, uint8_t secs);
void AVCLAN_normalizeState();

// Serialize cd_status into the wire format (track/mins/secs as BCD).
void serializeCDStatus(uint8_t *dst);

AVCLAN_frame_t *AVCLAN_getStatusFrame();
void AVCLAN_generateStatus(AVCLAN_frame_t *status, bool is_unicast, devices to);

#ifdef __cplusplus
}
#endif
