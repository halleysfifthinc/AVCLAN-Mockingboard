// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Bluetooth platform: the controller and the context Bluetooth runs on.
// Implemented by ports that build Bluetooth.

#pragma once

namespace avclan::bluetooth::platform {

// One-time bring-up of the Bluetooth controller and its context, which
// completes once the scheduler runs.
void init();

} // namespace avclan::bluetooth::platform
