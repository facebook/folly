/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <concepts>
#include <iterator>
#include <type_traits>
#include <utility>

namespace folly {

/**
 * folly::reverse_iterator
 *
 * Same semantics as std::reverse_iterator, but it reports triviality
 * correctly. Both libstdc++ and libc++ user-provide the copy constructor of
 * std::reverse_iterator, so std::is_trivially_copyable_v is false even when
 * the underlying iterator is a raw pointer. Here every special member is
 * implicitly defaulted, so triviality follows the underlying iterator.
 */
template <typename Iter>
class reverse_iterator {
  using traits = std::iterator_traits<Iter>;

  template <typename>
  friend class reverse_iterator;

 public:
  using iterator_type = Iter;
  using value_type = typename traits::value_type;
  using difference_type = typename traits::difference_type;
  using pointer = typename traits::pointer;
  using reference = typename traits::reference;
  // Reversing never yields more than random access, even over a contiguous
  // iterator.
  using iterator_category = std::conditional_t<
      std::is_convertible_v<
          typename traits::iterator_category,
          std::random_access_iterator_tag>,
      std::random_access_iterator_tag,
      typename traits::iterator_category>;
  using iterator_concept = std::conditional_t<
      std::random_access_iterator<Iter>,
      std::random_access_iterator_tag,
      std::bidirectional_iterator_tag>;

  constexpr reverse_iterator() = default;

  explicit constexpr reverse_iterator(Iter it) noexcept(
      std::is_nothrow_move_constructible_v<Iter>)
      : current_{std::move(it)} {}

  template <typename OtherIter>
    requires(
        !std::is_same_v<OtherIter, Iter> &&
        std::convertible_to<const OtherIter&, Iter>)
  /* implicit */ constexpr reverse_iterator(
      const reverse_iterator<OtherIter>& other)
      : current_{other.current_} {}

  template <typename OtherIter>
    requires(
        !std::is_same_v<OtherIter, Iter> &&
        std::convertible_to<const OtherIter&, Iter> &&
        std::assignable_from<Iter&, const OtherIter&>)
  constexpr reverse_iterator& operator=(
      const reverse_iterator<OtherIter>& other) {
    current_ = other.current_;
    return *this;
  }

  constexpr Iter base() const { return current_; }

  constexpr reference operator*() const {
    Iter tmp = current_;
    return *--tmp;
  }

  constexpr pointer operator->() const
    requires(
        std::is_pointer_v<Iter> || requires(const Iter i) { i.operator->(); })
  {
    Iter tmp = current_;
    --tmp;
    if constexpr (std::is_pointer_v<Iter>) {
      return tmp;
    } else {
      return tmp.operator->();
    }
  }

  constexpr reference operator[](difference_type n) const {
    return *(*this + n);
  }

  constexpr reverse_iterator& operator++() {
    --current_;
    return *this;
  }

  constexpr reverse_iterator operator++(int) {
    reverse_iterator tmp = *this;
    --current_;
    return tmp;
  }

  constexpr reverse_iterator& operator--() {
    ++current_;
    return *this;
  }

  constexpr reverse_iterator operator--(int) {
    reverse_iterator tmp = *this;
    ++current_;
    return tmp;
  }

  constexpr reverse_iterator& operator+=(difference_type n) {
    current_ -= n;
    return *this;
  }

  constexpr reverse_iterator& operator-=(difference_type n) {
    current_ += n;
    return *this;
  }

  constexpr reverse_iterator operator+(difference_type n) const {
    return reverse_iterator{current_ - n};
  }

  constexpr reverse_iterator operator-(difference_type n) const {
    return reverse_iterator{current_ + n};
  }

  friend constexpr reverse_iterator operator+(
      difference_type n, const reverse_iterator& it) {
    return it + n;
  }

 private:
  Iter current_{};
};

template <typename Iter>
reverse_iterator(Iter) -> reverse_iterator<Iter>;

template <typename Iter1, typename Iter2>
constexpr bool operator==(
    const reverse_iterator<Iter1>& a, const reverse_iterator<Iter2>& b) {
  return a.base() == b.base();
}

template <typename Iter1, typename Iter2>
constexpr bool operator<(
    const reverse_iterator<Iter1>& a, const reverse_iterator<Iter2>& b) {
  return b.base() < a.base();
}

template <typename Iter1, typename Iter2>
constexpr bool operator>(
    const reverse_iterator<Iter1>& a, const reverse_iterator<Iter2>& b) {
  return b.base() > a.base();
}

template <typename Iter1, typename Iter2>
constexpr bool operator<=(
    const reverse_iterator<Iter1>& a, const reverse_iterator<Iter2>& b) {
  return b.base() <= a.base();
}

template <typename Iter1, typename Iter2>
constexpr bool operator>=(
    const reverse_iterator<Iter1>& a, const reverse_iterator<Iter2>& b) {
  return b.base() >= a.base();
}

template <typename Iter1, typename Iter2>
constexpr auto operator-(
    const reverse_iterator<Iter1>& a, const reverse_iterator<Iter2>& b)
    -> decltype(b.base() - a.base()) {
  return b.base() - a.base();
}

template <typename Iter>
constexpr reverse_iterator<Iter> make_reverse_iterator(Iter it) {
  return reverse_iterator<Iter>{std::move(it)};
}

} // namespace folly
