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

#if !defined(__cpp_lib_to_chars) || __cpp_lib_to_chars < 201611L
  #include <cstdint>
  #include <type_traits>

namespace avclan {
// No <charconv> (nor the <system_error> its result type needs) in the
// freestanding stdlib, so mirror the std API surface for the unsigned-integral
// overload — the only one used here. Values are those of std::errc, so call
// sites written against either spelling behave the same.
enum class errc : uint8_t { value_too_large = 75 };

struct to_chars_result {
  char *ptr;
  errc ec;
};

template <class T>
  requires std::is_integral_v<T> && std::is_unsigned_v<T>
constexpr to_chars_result to_chars(char *first, char *last, T value,
                                   int base = 10) {
  char digits[sizeof(T) * 8]; // worst case: base 2
  uint8_t ndigits = 0;
  do {
    const auto digit = static_cast<unsigned>(value % static_cast<T>(base));
    digits[ndigits++] =
        static_cast<char>(digit < 10 ? '0' + digit : 'a' + (digit - 10));
    value = static_cast<T>(value / static_cast<T>(base));
  } while (value != 0);

  if ((last - first) < ndigits)
    return {.ptr = last, .ec = errc::value_too_large};

  while (ndigits > 0)
    *first++ = digits[--ndigits];
  return {.ptr = first, .ec = errc{}};
}
} // namespace avclan
#else
  #include <charconv>

namespace avclan {
using errc = std::errc;
using std::to_chars;
using std::to_chars_result;
} // namespace avclan
#endif
