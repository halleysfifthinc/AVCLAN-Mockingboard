// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <concepts>
#include <cstdint>
#include <limits>
#include <memory>

namespace detail {
template <class T, auto N> struct Deleter;
}

template <class T, std::integral auto N, bool Owning = false>
  requires((N & (N - 1)) == 0 && N <= std::numeric_limits<uint8_t>::max())
class Queue {
  using Deleter = detail::template Deleter<T, N>;
  friend Deleter;

public:
  // Only full and copy-convert-from-full construction is allowed
  Queue() = delete;
  Queue(const Queue &) = delete;

  // Moving is unsupported due to being self-referential
  Queue(Queue &&) = delete;
  Queue &operator=(Queue &&) = delete;

  constexpr Queue(T (&items)[N])
    requires(Owning)
      : owner{this}, write{N} {
    for (uint8_t i = 0; i < N; ++i)
      buf[i] = &items[i];
  }
  constexpr Queue(Queue<T, N, true> &queue)
    requires(!Owning)
      : owner{&queue} {}

  bool isEmpty() const { return write == read; }
  uint8_t size() const { return write - read; }
  uint8_t capacity() const { return N; }
  bool isFull() const { return size() == capacity(); }

  uint8_t push(std::unique_ptr<T, Deleter> x)
    requires(!Owning)
  {
    // structurally unnecessary; empty construction from same size parent
    // guarantees
    // !isFull assert(!isFull());

    if (!x || !x.get_deleter().is_owned_by(owner))
      return 1;

    reclaim(x.release());

    return 0;
  }

  const T *peek() const {
    if (isEmpty())
      return nullptr;

    return buf[mask(read)];
  }

  std::unique_ptr<T, Deleter> pop() {
    if (isEmpty())
      return nullptr;

    if constexpr (Owning)
      return {buf[mask(read++)], {this}};
    else
      return {buf[mask(read++)], {owner}};
  }

private:
  uint8_t mask(uint8_t pos) const { return pos & (N - 1); }
  void reclaim(T *x) { buf[mask(write++)] = x; }

  std::array<T *, N> buf = {};
  Queue<T, N, true> *const owner;
  uint8_t read = 0;
  uint8_t write = 0;
};

template <class T, auto N> Queue(Queue<T, N, true> &) -> Queue<T, N, false>;
template <class T, auto N> Queue(T (&items)[N]) -> Queue<T, N, true>;

namespace detail {
template <class T, auto N> struct Deleter {
  Deleter() = default;
  constexpr Deleter(Queue<T, N, true> *owner) : owner{owner} {}
  void operator()(T *x) const { owner->reclaim(x); }
  constexpr bool is_owned_by(const Queue<T, N, true> *parent) const {
    return owner == parent;
  }

private:
  Queue<T, N, true> *const owner = nullptr;
};
} // namespace detail
