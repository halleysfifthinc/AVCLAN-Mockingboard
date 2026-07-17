// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <concepts>
#include <cstdint>
#include <limits>
#include <memory>
#include <type_traits>

namespace detail {
template <class T, auto N> struct Deleter;
}

template <class T, std::integral auto N, bool Owning = false,
          class Deleter = std::default_delete<T>>
  requires((N & (N - 1)) == 0 && N <= std::numeric_limits<uint8_t>::max() &&
           // push() drops the passed-in deleter and pop() fabricates a fresh
           // one, so a deleter must either derive its state from the queue
           // itself (the pool Deleter) or carry no state at all
           (std::is_same_v<Deleter, detail::Deleter<T, N>> ||
            (std::is_empty_v<Deleter> &&
             std::is_nothrow_default_constructible_v<Deleter>)))
class Queue {
  friend detail::Deleter<T, N>;

public:
  // Only empty construction is allowed for non-Owning, non-pool deleters
  constexpr Queue()
    requires(!Owning && !std::is_same_v<Deleter, detail::Deleter<T, N>>)
      : owner{nullptr} {}
  Queue(const Queue &) = delete;

  // Moving is unsupported due to being self-referential
  Queue(Queue &&) = delete;
  Queue &operator=(Queue &&) = delete;

  // Construct from pre-defined storage
  constexpr Queue(T (&items)[N])
    requires(Owning) && std::same_as<Deleter, detail::Deleter<T, N>>
      : owner{this}, write{N} {
    for (uint8_t i = 0; i < N; ++i)
      buf[i] = &items[i];
  }
  // Construct non-Owning empty from Owning queue
  constexpr Queue(Queue<T, N, true, Deleter> &queue)
    requires(!Owning)
      : owner{&queue} {}

  bool isEmpty() const { return write == read; }
  uint8_t size() const { return write - read; }
  uint8_t capacity() const { return N; }
  bool isFull() const { return size() == capacity(); }

  uint8_t push(std::unique_ptr<T, Deleter> x)
    requires(!Owning)
  {
    if constexpr (!std::is_same_v<Deleter, detail::Deleter<T, N>>) {
      // structurally unnecessary for detail::Deleter, where empty construction
      // is only from same size parent
      if (isFull())
        return 1;
    }

    if (!x)
      return 1;

    if constexpr (std::is_same_v<Deleter, detail::Deleter<T, N>>) {
      if (!x.get_deleter().is_owned_by(owner))
        return 1;
    }

    claim(x.release());

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

    if constexpr (!std::is_same_v<Deleter, detail::Deleter<T, N>>)
      return {buf[mask(read++)], Deleter{}};
    else if constexpr (Owning)
      return {buf[mask(read++)], Deleter{this}};
    else
      return {buf[mask(read++)], Deleter{owner}};
  }

private:
  uint8_t mask(uint8_t pos) const { return pos & (N - 1); }
  void claim(T *x) { buf[mask(write++)] = x; }

  std::array<T *, N> buf = {};
  Queue<T, N, true, Deleter> *const owner;
  uint8_t read = 0;
  uint8_t write = 0;
};

template <class T, auto N, class D>
Queue(Queue<T, N, true, D> &) -> Queue<T, N, false, D>;
template <class T, auto N>
Queue(T (&items)[N]) -> Queue<T, N, true, detail::Deleter<T, N>>;

namespace detail {
template <class T, auto N> struct Deleter {
  Deleter() = default;
  constexpr Deleter(Queue<T, N, true, Deleter> *owner) : owner{owner} {}
  void operator()(T *x) const { owner->claim(x); }
  constexpr bool is_owned_by(const Queue<T, N, true, Deleter> *parent) const {
    return owner == parent;
  }

private:
  Queue<T, N, true, Deleter> *const owner = nullptr;
};
} // namespace detail
