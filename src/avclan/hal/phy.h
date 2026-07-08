// copyright (C) 2006 Marcin Slonicki <marcin@softservice.com.pl>
// copyright (C) 2007 Louis Frigon
// Copyright (C) 2015 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdint.h>

#include "avclan.h"

#ifdef __cplusplus
using Read = avclan::detail::Error::Read;
using Send = avclan::detail::Error::Send;
using Bit = avclan::detail::Bit;
extern "C" {
#else
typedef enum Read Read;
typedef enum Send Send;
typedef enum Bit Bit;
#endif

// One-time bring-up of the bus hardware. Leaves the bus idle and TX unmuted.
void phy_init(void);

// Mute/unmute device TX. "Muted" means transmission is disabled (RX is
// unchanged/still allowed)
void phy_mute(bool mute);

// Non-mutating (e.g. theoretically const qualified/-able)
bool phy_is_muted(void);

// True when bus is driven/"dominant" (logical 0)
bool phy_active(void);

// Bus-transaction guard: quiesce the other async sources (e.g. interrupts)
// so that bus read/send timing isn't disturbed. Re-enable relevant async
// sources with `phy_guard_leave`. May be a no-op on a target where contention
// isn't a concern.
void phy_guard_enter(void);
void phy_guard_leave(void);

// Start-bit handling, factored out of read/sendframe so the framing layer holds
// no bus-timing or hardware-recovery logic.
// - phy_read_startbit waits for and validates an incoming start bit, doing
//   any target-specific bus recovery; see avclan::detail::Error::Read.
// - phy_send_startbit acquires the bus and emits a start bit; may return BUSY
Read phy_read_startbit(void);
Send phy_send_startbit(void);

/* Returns 0 (`(Send)0`) if the peripheral sent an ACK bit, otherwise returns
NAK. An ACK bit is a cooperative bit, where the sender starts (drives the bus)
for the sync period, and allows the receiver to drive the bus (or not) to
finish a "1" bit.
*/
Send phy_read_ack(void);
void phy_send_ack(void);

// Per-symbol I/O. The send* helpers return the even parity of the bits sent;
// the read* helpers return the even parity of the bits read. The _u8/_u16
// suffixes name the source-operand width. The function implementations need not
// all be separate/independent (e.g. all send functions could be redirect to a
// single phy_send_bits_u16, etc).
// N.B: `len` is the number of bits  to send. The
// C++ send/readbits templates are the only consumers and use constraints to
// enforce valid len values, so runtime checks are unnecessary.

// Intended for sending parity bits
void phy_send_bit(Bit bit);

// Variants available to minimize unnecessary work for max runtime efficiency
Bit phy_send_bits_u8(const uint8_t *bits, int8_t len);
Bit phy_send_bits_u16(const uint16_t *bits, int8_t len);
Bit phy_send_byte(const uint8_t *byte);

Bit phy_read_bits_u8(uint8_t *bits, uint8_t len);
Bit phy_read_bits_u16(uint16_t *bits, int8_t len);
Bit phy_read_byte(uint8_t *byte);

#ifndef NDEBUG
// Sample and dump bus bit timing over the serial link (REPL `M`).
void phy_measure(void);
#endif

#ifdef __cplusplus
}
#endif
