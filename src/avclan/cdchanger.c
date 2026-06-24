/*
                        AVCLAN-Mockingboard
    Copyright (C) 2015 Allen Hill <allenofthehills@gmail.com>

    Portions of the following source code are based on code that is
    copyright (C) 2006 Marcin Slonicki <marcin@softservice.com.pl>
    copyright (C) 2007 Louis Frigon

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

#include <stdint.h>
#include <string.h>

#include "avclan_phy.h"
#include "cdchanger.h"
#include "mediacontrol.h"
#include "statustimer.h"

AVCLAN_CD_Status_t cd_status;

static cd_modes CD_Mode;

// Sets CD_mode to play and resets timer count (so that the next interrupt is in
// 1 sec)
void AVCLAN_startPlaying() {
  static bool havePlayed = false;
  if (havePlayed)
    AVCLAN_mediaFunction(MEDIA_PLAY_PAUSE);
  havePlayed |= true;
  CD_Mode = stPlay;
  statustimer_reset();
}

// Sets CD_mode to play and resets timer count (so that the next interrupt is in
// 1 sec)
void AVCLAN_stopPlaying() {
  statustimer_disable();
  CD_Mode = stStop;
  AVCLAN_mediaFunction(MEDIA_PLAY_PAUSE);
}

/* Pack a 0–99 count into 2-digit BCD. Values >99 (sentinels such as 0xFF /
   0x7F meaning "no time") pass through unchanged so they survive the wire
   round-trip. */
static uint8_t toBCD(uint8_t x) {
  if (x > 99)
    return x;
  return (uint8_t)(((x / 10) << 4) | (x % 10));
}

// Serialize cd_status into the wire format. The struct layout mirrors the wire
// format byte-for-byte, except for track/mins/secs, which need converted from
// decimal to BCD
void serializeCDStatus(uint8_t *dst) {
  memcpy(dst, &cd_status, sizeof(cd_status));
  dst[3] = toBCD(cd_status.track);
  dst[4] = toBCD(cd_status.mins);
  dst[5] = toBCD(cd_status.secs);
}

bool AVCLAN_isPlaying() { return (CD_Mode == stPlay); }

void AVCLAN_incrementTime() {
  // Sentinel values (>99) mean "no time"; leave them alone until setTime()
  // replaces them with a real count.
  if (cd_status.secs > 99)
    return;
  if (cd_status.secs == 59) {
    cd_status.secs = 0;
    if (cd_status.mins == 99)
      cd_status.mins = 0;
    else
      cd_status.mins++;
  } else
    cd_status.secs++;
}

void AVCLAN_setTime(uint8_t mins, uint8_t secs) {
  cd_status.mins = mins;
  cd_status.secs = secs;
}

// Only used for regularly scheduled periodic updates
AVCLAN_frame_t *AVCLAN_getStatusFrame() {
  static uint8_t status_data[sizeof(AVCLAN_CD_Status_t) + 3] = {0};
  static AVCLAN_frame_t status = {.is_unicast = false,
                                  .controller_addr = DEVICE_ADDR,
                                  .peripheral_addr = 0x1FF,
                                  .control = 0xF,
                                  .length = sizeof(status_data),
                                  .data = status_data};

  return &status;
}

// Used for changed status messages
void AVCLAN_generateStatus(AVCLAN_frame_t *status, bool is_unicast,
                           devices to) {
  *status = (AVCLAN_frame_t){
      .is_unicast = is_unicast,
      .controller_addr = DEVICE_ADDR,
      .peripheral_addr = (is_unicast) ? HU_ADDR : 0x1FF,
      .control = 0xF,
      .length = sizeof(AVCLAN_CD_Status_t) + ((is_unicast) ? 4 : 3),
      .data = status->data, // don't overwrite data pointer
  };

  uint8_t *data = status->data;
  if (is_unicast)
    *data++ = 0x00;
  *data++ = dev_CD_CHANGER;
  *data++ = to;
  *data++ = Status_Report;
  serializeCDStatus(data);
}

void AVCLAN_normalizeState() {
  // if (cd_status.state != cd_PLAYBACK) {
  if (cd_status.mins > 99)
    cd_status.mins = 0;
  if (cd_status.secs > 99)
    cd_status.secs = 0;
  cd_status.state = cd_PLAYBACK;
  cd_status.flags &= (uint8_t)~(cd_DISK_SCAN | cd_SCAN);
  cd_status.flags2 = 0x80;
  // }
}

void AVCLAN_init() {
  AVCLAN_busInit();
  mediacontrol_init();
  statustimer_init();

  cd_status.cds = cd_CD1;
  cd_status.disc = 1;
  cd_status.state = cd_SEEKING | cd_SEEKING_TRACK;
  cd_status.flags = 0;
  cd_status.flags2 = 0xC0;

  cd_status.track = 1;
  cd_status.mins = 0xFF;
  cd_status.secs = 0x7F;

  CD_Mode = stStop;
}
