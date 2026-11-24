// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Bluetooth platform: the controller and the context BTstack runs on.
// Implemented by ports that build Bluetooth.

#pragma once

namespace avclan::bluetooth::platform {

// One-time bring-up of the Bluetooth controller and BTstack's context, which
// completes once the scheduler runs. `setup(ctx)` then runs once on that
// context to configure BTstack and power it on.
void init(void (*setup)(void *), void (*work)(void *), void *ctx);

// Run `work(ctx)` on BTstack's context. Callable from any task and never
// blocks; wakes made before `work` runs coalesce.
void wake();

} // namespace avclan::bluetooth::platform
