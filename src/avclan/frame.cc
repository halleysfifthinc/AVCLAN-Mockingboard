// copyright (C) 2006 Marcin Slonicki <marcin@softservice.com.pl>
// copyright (C) 2007 Louis Frigon
// Copyright (C) 2015 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

#include "frame.hpp"
#include "hal/stdio.h"
#include "stdshim.hpp"

#if defined(AVCLAN_FRAME_POOL_N)
  #include <array>
  #include <cstddef>
  #include <limits>
  #include <new>

namespace {
template <class T, std::uint8_t N>
  requires(N >= 1 && N <= std::numeric_limits<uint8_t>::max())
class Pool {
public:
  constexpr Pool() {
    for (uint8_t i = 0; i < N; ++i)
      ptrs_[i] = &storage_[i];
  }

  T *acquire() {
    if (top_ == 0)
      return nullptr;
    return ptrs_[--top_];
  }

  void release(T *ptr) {
    // Properly would need an origin check/confirmation if this was used more
    // generally
    ptrs_[top_++] = ptr;
  }

private:
  std::array<T, N> storage_;
  std::array<T *, N> ptrs_;
  uint8_t top_ = N;
};

Pool<avclan::Frame, AVCLAN_FRAME_POOL_N> pool;
} // namespace
#endif

namespace {
using Error = avclan::detail::Error;
using enum Error::Parse;
} // namespace

namespace avclan {

#if defined(AVCLAN_FRAME_POOL_N)
void *Frame::operator new(std::size_t /*count*/,
                          const std::nothrow_t & /*tag*/) noexcept {
  return pool.acquire();
}
// NOLINTNEXTLINE(misc-new-delete-overloads) false-positive
void Frame::operator delete(void *ptr) noexcept {
  pool.release(static_cast<Frame *>(ptr));
}
#endif

namespace {
// Emit `value` as at least `width` (lowercase, as to_chars emits) hex digits,
// zero-padded. `width` must be <= 3 (see the padding below); every call site
// reserves its field's worst case in the destination buffer, so this can't
// overflow.
template <class T>
  requires std::is_unsigned_v<T>
char *put_hex(char *dest, T value, uint8_t width) {
  // to_chars emits the minimum number of digits, so work out up front how many
  // that will be to know how much zero padding goes in front of them. At most
  // two iterations for the field widths used here.
  uint8_t ndigits = 1;
  for (T rest = value; rest >= 16; rest /= 16)
    ndigits++;

  // At most two pad digits (width <= 3). Written out rather than as a counted
  // loop because GCC turns that into a memset() call — call overhead an order
  // of magnitude above the one or two stores it replaces.
  if (ndigits < width) {
    *dest++ = '0';
    if (ndigits + 1 < width)
      *dest++ = '0';
  }

  return to_chars(dest, dest + ndigits, value, 16).ptr;
}

// " 0xNNN" for each field, plus the leading unicast digit and trailing newline
constexpr uint8_t LINE_MAX = 1 + (4 * 6) + (Frame::MAXLENGTH * 5) + 1;
// The same buffer serves the binary branch, which is the shorter of the two
static_assert(LINE_MAX >= 8 + Frame::MAXLENGTH + 2);
} // namespace

void Frame::print(Frame::Print print) const {
  // The AVC-LAN read loop polls for start bits between calls, so emitting per
  // character (~3 µs each through stdio) would blow past a start bit's ~169 µs
  // and lose the next frame.
  char buffer[LINE_MAX];
  char *bptr = buffer;

  if (print.binary) {
    *bptr++ = 0x10; // Data Link Escape, signaling binary data forthcoming
    *bptr++ = static_cast<char>(is_unicast);

    // Send addresses in big-endian order
    *bptr++ = static_cast<char>(controller_addr >> 8);
    *bptr++ = static_cast<char>(controller_addr);
    *bptr++ = static_cast<char>(peripheral_addr >> 8);
    *bptr++ = static_cast<char>(peripheral_addr);

    *bptr++ = static_cast<char>(control);
    *bptr++ = static_cast<char>(length);

    memcpy(bptr, data, length);
    bptr += length;

    *bptr++ = 0x17; // End of transmission block
    *bptr++ = 0x0A; // \n

    stdio_write_nonblock(buffer, static_cast<size_t>(bptr - buffer));
    return;
  }

  struct field_t {
    uint16_t value;
    uint8_t width; // minimum digits, matching the old %03x / %x formats
  };

  *bptr++ = is_unicast ? '1' : '0';
  for (const field_t field :
       {field_t{.value = static_cast<uint16_t>(controller_addr & 0x0FFF),
                .width = 3},
        field_t{.value = static_cast<uint16_t>(peripheral_addr & 0x0FFF),
                .width = 3},
        field_t{.value = static_cast<uint16_t>(control & 0x0F), .width = 1},
        field_t{.value = static_cast<uint16_t>(length & 0x0F), .width = 1}}) {
    *bptr++ = ' ';
    *bptr++ = '0';
    *bptr++ = 'x';
    bptr = put_hex(bptr, field.value, field.width);
  }

  for (uint8_t i = 0; i < length; i++) {
    *bptr++ = ' ';
    *bptr++ = '0';
    *bptr++ = 'x';
    bptr = put_hex(bptr, data[i], 2);
  }
  *bptr++ = '\n';

  stdio_write_nonblock(buffer, static_cast<size_t>(bptr - buffer));
}

Error::Parse Frame::parse(const uint8_t *bytes, uint8_t len) {
  struct errtype {
    Error::Parse errno;
    uint8_t val;
  } err = {};

  const uint8_t *last = bytes + len;

  if (len < Frame::MIN_SIZE) {
    err.errno = TOO_SHORT;
    goto handle_err;
  }

  is_unicast = (*bytes++ != 0U);
  controller_addr = bytes[0] | ((uint16_t)bytes[1] << 8);
  bytes += 2;
  peripheral_addr = bytes[0] | ((uint16_t)bytes[1] << 8);
  bytes += 2;
  control = *bytes++;
  length = *bytes++;

  if (length > MAXLENGTH) {
    err.errno = LENGTH_TOO_BIG;
    err.val = length;
    goto handle_err;
  }

  if ((bytes + length) <= last) {
    memcpy(data, bytes, length);
  } else {
    err.errno = MISMATCH_LENGTH;
    goto handle_err;
  }

  if (false) { // NOLINT(readability-simplify-boolean-expr)
  handle_err:;
    fputs("ERR(parse): ", stdout);
    switch (err.errno) {
      case TOO_SHORT: puts("not enough bytes too fill AVCLAN frame"); break;
      case MISMATCH_LENGTH:
        puts("frame->length is longer than remaining data");
        break;
      case LENGTH_TOO_BIG:
        printf("frame->length exceeds MAXLENGTH: 0x%02X\n",
               static_cast<unsigned>(err.val));
        break;
      default: break;
    }
  }

  return err.errno;
}
} // namespace avclan
