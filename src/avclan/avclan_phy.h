// copyright (C) 2006 Marcin Slonicki <marcin@softservice.com.pl>
// copyright (C) 2007 Louis Frigon
// Copyright (C) 2015 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AVCLAN_PHY_H
#define AVCLAN_PHY_H

#include <stdint.h>

#include "avclan_defs.h"

// One-time bring-up of the bus hardware. Leaves the bus idle and TX unmuted.
void AVCLAN_busInit(void);

// Mute/unmute device TX. "Muted" means we still listen, we just don't ACK or
// transmit.
void AVCLAN_muteDevice(bool mute);
bool AVCLAN_ismuted(void);

// True when there is activity on the bus (something is driving it).
bool AVCLAN_busActive(void);

// Bus-transaction guard: quiesce the target's other async sources around a bus
// read/send so framing isn't disturbed, then restore them. May be a no-op on a
// target without such contention.
void AVCLAN_stopEvent(void);
void AVCLAN_startEvent(void);

// Start-bit handling, factored out of read/sendframe so the framing layer holds
// no bus-timing or hardware-recovery logic.
// - AVCLAN_readstartbit waits for and validates an incoming start bit, doing
//   any target-specific bus recovery; see avclan_readerr_t.
// - AVCLAN_sendstartbit acquires the bus and emits a start bit; returns false
//   if the bus was busy.
avclan_readerr_t AVCLAN_readstartbit(void);
bool AVCLAN_sendstartbit(void);

// Per-symbol I/O. The send* helpers return the even parity of the bits sent;
// the read* helpers return the even parity of the bits read.
void AVCLAN_sendbit(avclan_bit_t bit);
void AVCLAN_sendbit_ACK(void);
uint8_t AVCLAN_readbit_ACK(void);

avclan_bit_t AVCLAN_sendbitsi(const uint8_t *bits, int8_t len);
avclan_bit_t AVCLAN_sendbitsl(const uint16_t *bits, int8_t len);
avclan_bit_t AVCLAN_sendbyte(const uint8_t *byte);

uint8_t AVCLAN_readbitsi(uint8_t *bits, uint8_t len);
uint8_t AVCLAN_readbitsl(uint16_t *bits, int8_t len);
uint8_t AVCLAN_readbyte(uint8_t *byte);

#define AVCLAN_sendbits(bits, len)                                             \
  _Generic((bits),                                                             \
      const uint16_t *: AVCLAN_sendbitsl,                                      \
      uint16_t *: AVCLAN_sendbitsl,                                            \
      const uint8_t *: AVCLAN_sendbitsi,                                       \
      uint8_t *: AVCLAN_sendbitsi)(bits, len)

#define AVCLAN_readbits(bits, len)                                             \
  _Generic((bits),                                                             \
      const uint16_t *: AVCLAN_readbitsl,                                      \
      uint16_t *: AVCLAN_readbitsl,                                            \
      const uint8_t *: AVCLAN_readbitsi,                                       \
      uint8_t *: AVCLAN_readbitsi)(bits, len)

#ifndef NDEBUG
// Sample and dump bus bit timing over the serial link (REPL `M`).
void AVCLan_Measure(void);
#endif

#endif // AVCLAN_PHY_H
