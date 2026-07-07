// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Generic stdio interface initialization. All user I/O goes through <stdio.h>
// functions. Assumptions/invariants:
//  - stdin MUST be non-blocking (ie. getchar() returns EOF immediately when
//    empty). Necessary to avoid stalling the REPL poll loop.
//  - stdout is *raw* — no '\n' -> "\r\n" translation: bare LF and binary frame
//    payloads (fwrite) are emitted verbatim
void stdio_init(void);

#ifdef __cplusplus
}
#endif
