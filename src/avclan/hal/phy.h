// copyright (C) 2006 Marcin Slonicki <marcin@softservice.com.pl>
// copyright (C) 2007 Louis Frigon
// Copyright (C) 2015 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdint.h>

#include "FreeRTOS.h" // IWYU pragma: export

#include "avclan.h"

#ifdef __cplusplus
using Read = avclan::detail::Error::Read;
using Send = avclan::detail::Error::Send;
extern "C" {
#else
typedef enum Read Read;
typedef enum Send Send;
#endif

// One-time bring-up of the bus hardware. Leaves the bus idle and TX unmuted.
//
// `address` is our device address.
void phy_init(uint16_t address);

// Mute/unmute device TX. "Muted" means transmission is disabled (RX is
// unchanged/still allowed)
void phy_mute(bool mute);

// Non-mutating (e.g. theoretically const qualified/-able)
bool phy_is_muted(void);

// Do not respond to or log incoming frames. Can still send frames.
void phy_deafen(bool deaf);

// Blocks until a frame is ready
void phy_wait_frame(TickType_t xTicksToWait);

// Bus-transaction guard: quiesce the other async sources (e.g. interrupts)
// so that bus read/send timing isn't disturbed. Re-enable relevant async
// sources with `phy_guard_leave`.
//  - May be a no-op on a target where concurrency/preemption isn't a concern.
void phy_guard_enter(void);
void phy_guard_leave(void);

/* Per-field frame I/O.
 *
 * All send functions may have asynchronous implementations (e.g. return before
 * the send has completed on the bus). Success is indicated by a zero value
 * `Read` or `Send` enum. Non-zero error codes indicate a synchronously
 * completed send failure. Otherwise, `phy_send_done` must be called to block
 * until all queued send's have completed, and may return the error code for a
 * previous (queued) send failure; a success return value indicates that all
 * queued send's have finished sending over the bus.
 *
 * The read functions are similarly optionally asynchronous, and may return the
 * results of buffered reads. When this is the case, phy_read_header returns the
 * relevant error for the entire frame.
 *
 * It is invalid to call any later read or send function after getting an
 * error (e.g. calling `phy_read_data` after `phy_read_length` errored).
 *
 * `expect_ack` indicates whether the recipient should be ACK'ing; false for
 * broadcast frames which don't have a single recipient.
 */

// Returns NO_FRAME when there is no frame to read.
Read phy_read_header(bool *is_unicast);
Read phy_read_controller_addr(uint16_t *addr);
Read phy_read_peripheral_addr(uint16_t *addr);
Read phy_read_control(uint8_t *control);
Read phy_read_length(uint8_t *length);
// Read `length` data bytes. `data_index` is only written to on error, with the
// index of the failed byte.
Read phy_read_data(uint8_t *data, uint8_t length, uint8_t *data_index);

// Send start and broadcast bits. Returns success or one of these error values:
// MUTED, BUSY, or LOST_ARBITRATION (if another device overrides our frame with
// a broadcast).
Send phy_send_header(bool is_unicast);

// Send the controller address. Returns success or LOST_ARBITRATION (a device
// with a lower device is sending a frame).
Send phy_send_controller_addr(uint16_t addr);
Send phy_send_peripheral_addr(uint16_t addr, bool expect_ack);
Send phy_send_control(uint8_t control, bool expect_ack);
Send phy_send_length(uint8_t length, bool expect_ack);
// Send `length` data bytes. `data_index` is only written to for NAK_DATA.
Send phy_send_data(const uint8_t *data, uint8_t length, bool expect_ack,
                   uint8_t *data_index);

// Allows asynchronous ports to block until the phy has finished sending the
// frame. Returns success or the relevant field-specific NAK (e.g. NAK_ADDRESS,
// etc) or CONTENDED_BUS; a port that queues the header and controller address
// also reports their BUSY or LOST_ARBITRATION here. `data_index` is only
// written to for NAK_DATA. A fully synchronous port should always report
// success.
Send phy_send_done(uint8_t *data_index);

#ifndef NDEBUG

// Hold the bus at a level until the matching call.
void phy_set_dominant(void);
void phy_set_recessive(void);

  #ifdef MEASURE_BUS
// Sample and dump bus bit timing over the serial link (REPL `M`).
void phy_measure(void);
  #endif
#endif

#ifdef __cplusplus
}
#endif
