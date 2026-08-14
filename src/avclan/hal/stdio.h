// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Generic stdio interface initialization. All user I/O goes through <stdio.h>
// functions. Assumptions/invariants:
//  - stdin MUST be non-blocking (ie. getchar() returns EOF immediately when
//    empty). Necessary to avoid stalling the REPL poll loop.
//  - stdout is *raw*. There is no '\n' -> "\r\n" translation. The port does not
//    change a bare LF or a binary frame payload. Writes through
//    <stdio.h> and writes through stdio_write_nonblock() must reach the same
//    stream, in order.
void stdio_init(void);

// Queue a buffer onto stdout--if it has space, or return false.
//
// If stdout cannot accept all `len` bytes now, the function queues
// a "\n!\n" indicator onto stdout, overwriting the last 3 chars if stdout is
// full. The caller must not assume that the buffer went out.
//
// A port MUST emit that indicator for every dropped buffer, and MUST be able to
// emit it even if stdout is full. A reader detects the loss as a line that
// holds only '!'. In the overwrite case the buffer in front of that line is
// truncated, so a reader MUST tolerate one malformed buffer there. The return
// value is useful only for diagnostics.
bool stdio_write_nonblock(const void *buf, size_t len);

#ifdef __cplusplus
}
#endif
