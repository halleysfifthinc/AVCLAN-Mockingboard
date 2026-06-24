// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef BOARD_H
#define BOARD_H

// Clock setup + GPIO/pin configuration. Call once, first thing at startup
// (before any peripheral init).
void board_init(void);

// Globally enable interrupts. Call after all peripherals are initialized.
void board_interruptsEnable(void);

#endif // BOARD_H
