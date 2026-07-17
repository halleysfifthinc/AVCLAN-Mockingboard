// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <version>

#if !defined(__cpp_lib_expected) || __cpp_lib_expected < 202211L
  #include <tl/expected.hpp>

namespace avclan {
template <class T, class E> using expected = tl::expected<T, E>;
template <class E> using unexpected = tl::unexpected<E>;
using tl::unexpect;
using tl::unexpect_t;
} // namespace avclan
#else
  #include <expected>

namespace avclan {
template <class T, class E> using expected = std::expected<T, E>;
template <class E> using unexpected = std::unexpected<E>;
using std::unexpect;
using std::unexpect_t;
} // namespace avclan
#endif
