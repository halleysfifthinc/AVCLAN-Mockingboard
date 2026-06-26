// copyright (C) 2006 Marcin Slonicki <marcin@softservice.com.pl>
// copyright (C) 2007 Louis Frigon
// Copyright (C) 2015 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdint.h>

#include "avclan_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

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

#ifndef NDEBUG
// Sample and dump bus bit timing over the serial link (REPL `M`).
void AVCLan_Measure(void);
#endif

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
template <class T, auto N> avclan_bit_t AVCLAN_sendbits(T bits);
template <class T, auto N> avclan_bit_t AVCLAN_readbits(T *bits);

// Temporary specializations bridging to legacy C API
// Replace with proper (single?) template when phy has been ported
template <auto N>
  requires(N <= 8)
avclan_bit_t AVCLAN_sendbits(uint8_t bits) {
  return AVCLAN_sendbitsi(&bits, N);
}
template <auto N>
  requires(N <= 16)
avclan_bit_t AVCLAN_sendbits(uint16_t bits) {
  return AVCLAN_sendbitsl(&bits, N);
}
template <> inline avclan_bit_t AVCLAN_sendbits<8>(uint8_t byte) {
  return AVCLAN_sendbyte(&byte);
}

template <auto N>
  requires(N <= 8)
avclan_bit_t AVCLAN_readbits(uint8_t *bits) {
  return static_cast<avclan_bit_t>(AVCLAN_readbitsi(bits, N));
}
template <auto N>
  requires(N <= 16)
avclan_bit_t AVCLAN_readbits(uint16_t *bits) {
  return static_cast<avclan_bit_t>(AVCLAN_readbitsl(bits, N));
}
template <> inline avclan_bit_t AVCLAN_readbits<8>(uint8_t *byte) {
  return static_cast<avclan_bit_t>(AVCLAN_readbyte(byte));
}
#endif
