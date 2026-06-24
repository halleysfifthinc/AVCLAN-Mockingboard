// copyright (C) 2006 Marcin Slonicki <marcin@softservice.com.pl>
// copyright (C) 2007 Louis Frigon
// Copyright (C) 2015 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

/*
        AVC LAN Frame Format
    │ Bits │ Description
    ────────────────────────────────────────
    |  1   │ Start bit
    |  1   │ Direct/broadcast
    |  12  │ Controller address
    |  1   │ Parity
    |  12  │ Peripheral address
    |  1   │ Parity
    |  1   │ *Acknowledge* (read below)
    |  4   │ Control
    |  1   │ Parity
    |  1   │ *Acknowledge*
    |  8   │ Message length (n)
    |  1   │ Parity
    |  1   │ *Acknowledge*
    ────────
       | 8 │ Data
       | 1 │ Parity
       | 1 │ *Acknowledge*
       *repeat `n` times*

  No acknowledge bits are sent for broadcast frames.
*/

#ifndef AVCLAN_FRAME_H
#define AVCLAN_FRAME_H

#include <stdint.h>

#include "avclan_defs.h"

avclan_readerr_t AVCLAN_readframe(AVCLAN_frame_t *frame, log_t print);
avclan_senderr_t AVCLAN_sendframe(const AVCLAN_frame_t *frame, log_t print);
void AVCLAN_printframe(const AVCLAN_frame_t *frame, bool binary);
uint8_t AVCLAN_parseframe(const uint8_t *bytes, uint8_t len,
                          AVCLAN_frame_t *frame);

#endif // AVCLAN_FRAME_H
