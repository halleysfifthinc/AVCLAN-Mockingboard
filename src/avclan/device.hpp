// copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "avclan.hpp"
#include "frame.hpp"

#include <concepts>
#include <cstdint>

namespace avclan {
template <class T>
concept Device = requires {
  std::integral_constant<uint8_t, T::id>{};
} && requires(T dev, const Frame *in, Frame *out, detail::Error::Send err) {
  dev.init();
  dev.handle(in, out);
  dev.enable(out);
  dev.react(out, err);
  { dev.pending() } -> std::convertible_to<bool>;
  dev.resolvepending();
  dev.emit(out);
};
} // namespace avclan
