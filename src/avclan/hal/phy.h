// copyright (C) 2006 Marcin Slonicki <marcin@softservice.com.pl>
// copyright (C) 2007 Louis Frigon
// Copyright (C) 2015 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdint.h>

#include "avclan.h"

#ifdef __cplusplus
using Read = avclan::detail::Error::Read;
using Bit = avclan::detail::Bit;
extern "C" {
#else
typedef enum Read Read;
typedef enum Bit Bit;
#endif

// One-time bring-up of the bus hardware. Leaves the bus idle and TX unmuted.
void phy_init(void);

// Mute/unmute device TX. "Muted" means we still listen, we just don't ACK or
// transmit.
void phy_mute(bool mute);
bool phy_is_muted(void);

// True when there is activity on the bus (something is driving it).
bool phy_active(void);

// Bus-transaction guard: quiesce the target's other async sources around a bus
// read/send so framing isn't disturbed, then restore them. May be a no-op on a
// target without such contention.
void phy_guard_enter(void);
void phy_guard_leave(void);

// Start-bit handling, factored out of read/sendframe so the framing layer holds
// no bus-timing or hardware-recovery logic.
// - phy_read_startbit waits for and validates an incoming start bit, doing
//   any target-specific bus recovery; see avclan::detail::Error::Read.
// - phy_send_startbit acquires the bus and emits a start bit; returns false
//   if the bus was busy.
Read phy_read_startbit(void);
bool phy_send_startbit(void);

// Per-symbol I/O. The send* helpers return the even parity of the bits sent;
// the read* helpers return the even parity of the bits read. The _u8/_u16
// suffixes name the source-operand width; `len` is how many bits (<= width).
void phy_send_bit(Bit bit);
void phy_send_ack(void);
uint8_t phy_read_ack(void);

Bit phy_send_bits_u8(const uint8_t *bits, int8_t len);
Bit phy_send_bits_u16(const uint16_t *bits, int8_t len);
Bit phy_send_byte(const uint8_t *byte);

uint8_t phy_read_bits_u8(uint8_t *bits, uint8_t len);
uint8_t phy_read_bits_u16(uint16_t *bits, int8_t len);
uint8_t phy_read_byte(uint8_t *byte);

#ifndef NDEBUG
// Sample and dump bus bit timing over the serial link (REPL `M`).
void phy_measure(void);
#endif

#ifdef __cplusplus
}
#endif
