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

#include <array>
#include <cassert>
#include <cstddef>
#include <limits>
#include <type_traits>

#include <folly/CppAttributes.h>
#include <folly/Portability.h>
#include <folly/Traits.h>
#include <folly/Utility.h>
#include <folly/container/Access.h>
#include <folly/container/Iterator.h>
#include <folly/functional/Invoke.h>
#include <folly/portability/Constexpr.h>

#if __cpp_lib_span >= 202002L
#include <span>
#endif

namespace folly {

namespace detail {

namespace fallback_span {

inline constexpr auto dynamic_extent = std::size_t(-1);

template <std::size_t N>
struct span_extent {
  constexpr span_extent() = default;
  explicit constexpr span_extent(
      [[maybe_unused]] std::size_t const e) noexcept {
    assert(e == N);
  }
  constexpr span_extent(span_extent const&) = default;
  constexpr span_extent& operator=(span_extent const&) = default;

  /* implicit */ constexpr operator std::size_t() const noexcept { return N; }
};

template <>
struct span_extent<dynamic_extent> {
  std::size_t extent{};

  constexpr span_extent() = default;
  explicit constexpr span_extent(std::size_t const e) noexcept : extent{e} {}
  constexpr span_extent(span_extent const&) = default;
  constexpr span_extent& operator=(span_extent const&) = default;

  /* implicit */ constexpr operator std::size_t() const noexcept {
    return extent;
  }
};

/// span
///
/// mimic: std::span, C++20
template <typename T, std::size_t Extent = dynamic_extent>
class span {
 public:
  static_assert(!std::is_reference_v<T>);
  static_assert(!std::is_void_v<T>);
  static_assert(!std::is_function_v<T>);
  static_assert(sizeof(T) < size_t(-1));
  static_assert(!std::is_abstract_v<T>);

  using element_type = T;
  using value_type = std::remove_cv_t<element_type>;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using pointer = element_type*;
  using const_pointer = element_type const*;
  using reference = element_type&;
  using const_reference = element_type const&;
  using iterator = pointer;
  using reverse_iterator = std::reverse_iterator<iterator>;

  static inline constexpr std::size_t extent = Extent;

 private:
  template <bool C>
  using if_ = std::enable_if_t<C, int>;

  template <typename U, typename V = std::remove_cv_t<U>>
  static inline constexpr bool array_element_match_v =
      std::is_same_v<V, value_type> && std::is_convertible_v<U*, pointer>;

  template <
      typename Rng,
      typename Size = remove_cvref_t<invoke_result_t<access::size_fn, Rng>>,
      typename Data = invoke_result_t<access::data_fn, Rng>,
      typename U = std::remove_pointer_t<Data>>
  static constexpr bool is_range_v =
      !std::is_same_v<bool, Size> && std::is_unsigned_v<Size> &&
      std::is_pointer_v<Data> && array_element_match_v<U>;

  static constexpr size_type subspan_extent(
      size_type const offset, size_type const count) {
    // clang-format off
    return
        count != dynamic_extent ? count :
        extent != dynamic_extent ? extent - offset :
        dynamic_extent;
    // clang-format on
  }

  pointer data_;
  [[FOLLY_ATTR_NO_UNIQUE_ADDRESS]] span_extent<extent> extent_;

 public:
  template <size_type E = extent, if_<E == dynamic_extent || E == 0> = 0>
  constexpr span() noexcept : data_{}, extent_{} {}

  constexpr span(pointer const first, size_type const count)
      : data_{first}, extent_{count} {}

  constexpr span(pointer const first, pointer const last)
      : data_{first}, extent_{to_unsigned(last - first)} {
    assert(!(last < first));
  }

  template <
      std::size_t N,
      std::size_t E = extent,
      if_<E == dynamic_extent || E == N> = 0>
  /* implicit */ constexpr span(type_t<element_type> (&arr)[N]) noexcept
      : data_{arr}, extent_{N} {}

  template <
      typename U,
      std::size_t N,
      std::size_t E = extent,
      if_<E == dynamic_extent || E == N> = 0,
      if_<array_element_match_v<U>> = 0>
  /* implicit */ constexpr span(std::array<U, N>& arr) noexcept
      : data_{arr.data()}, extent_{N} {}

  template <
      typename U,
      std::size_t N,
      std::size_t E = extent,
      if_<E == dynamic_extent || E == N> = 0,
      if_<array_element_match_v<U const>> = 0>
  /* implicit */ constexpr span(std::array<U, N> const& arr) noexcept
      : data_{arr.data()}, extent_{N} {}

  template <typename Rng, if_<is_range_v<Rng&>> = 0>
  /* implicit */ constexpr span(Rng&& range)
      : data_{access::data(range)}, extent_{access::size(range)} {}

  constexpr span(span const&) = default;

  constexpr span& operator=(span const&) = default;

  constexpr pointer data() const noexcept { return data_; }
  constexpr size_type size() const noexcept { return extent_; }
  constexpr size_type size_bytes() const noexcept {
    return size() * sizeof(element_type);
  }
  constexpr bool empty() const noexcept { return size() == 0; }

  constexpr iterator begin() const noexcept { return data_; }
  constexpr iterator end() const noexcept { return data_ + size(); }
  constexpr reverse_iterator rbegin() const noexcept {
    return std::make_reverse_iterator(begin());
  }
  constexpr reverse_iterator rend() const noexcept {
    return std::make_reverse_iterator(end());
  }

  constexpr reference front() const {
    assert(!empty());
    return data_[0];
  }
  constexpr reference back() const {
    assert(!empty());
    return data_[size() - 1];
  }
  constexpr reference operator[](size_type const idx) const {
    assert(idx < size());
    return data_[idx];
  }

  template <
      size_type Offset,
      size_type Count = dynamic_extent,
      typename...,
      size_type E = subspan_extent(Offset, Count)>
  constexpr span<element_type, E> subspan() const {
    static_assert(!(Extent < Offset));
    static_assert(Count == dynamic_extent || !(extent - Offset < Count));
    assert(!(size() < Offset));
    assert(Count == dynamic_extent || !(size() - Offset < Count));
    return {data_ + Offset, Count == dynamic_extent ? size() - Offset : Count};
  }

  constexpr span<element_type, dynamic_extent> subspan(
      size_type const offset, size_type const count = dynamic_extent) const {
    assert(!(extent < offset));
    assert(count == dynamic_extent || !(extent - offset < count));
    assert(!(size() < offset));
    assert(count == dynamic_extent || !(size() - offset < count));
    return {data_ + offset, count == dynamic_extent ? size() - offset : count};
  }

  template <size_type Count>
  constexpr span<element_type, Count> first() const {
    static_assert(!(extent < Count));
    assert(!(size() < Count));
    return {data_, Count};
  }
  constexpr span<element_type, dynamic_extent> first(
      size_type const count) const {
    assert(!(extent < count));
    assert(!(size() < count));
    return {data_, count};
  }

  template <size_type Count>
  constexpr span<element_type, Count> last() const {
    static_assert(!(extent < Count));
    assert(!(size() < Count));
    return {data_ + size() - Count, Count};
  }
  constexpr span<element_type, dynamic_extent> last(
      size_type const count) const {
    assert(!(extent < count));
    assert(!(size() < count));
    return {data_ + size() - count, count};
  }
};

template <typename T, typename EndOrSize>
span(T*, EndOrSize) -> span<T>;

template <typename T, std::size_t N>
span(T (&)[N]) -> span<T, N>;

template <typename T, std::size_t N>
span(std::array<T, N>&) -> span<T, N>;

template <typename T, std::size_t N>
span(const std::array<T, N>&) -> span<const T, N>;

template <typename R>
span(R&&) -> span<std::remove_reference_t<
              iterator_reference_t<decltype(std::begin(std::declval<R&>()))>>>;

} // namespace fallback_span

} // namespace detail

#if __cpp_lib_span >= 202002L

using std::dynamic_extent;
using std::span;

#else

using detail::fallback_span::dynamic_extent;
using detail::fallback_span::span;

#endif

namespace detail {

struct span_cast_impl_fn {
  template <
      template <typename, std::size_t>
      class Span,
      typename U,
      typename T,
      std::size_t Extent>
  constexpr auto operator()(Span<T, Extent> in, U* castData) const {
    assert(
        static_cast<void const*>(in.data()) ==
        static_cast<void const*>(castData));

    // check alignment
    if (!folly::is_constant_evaluated_or(true)) {
      assert(reinterpret_cast<std::uintptr_t>(in.data()) % sizeof(U) == 0);
    }

    if constexpr (Extent == dynamic_extent) {
      assert(in.size() * sizeof(T) % sizeof(U) == 0);
      return Span<U, dynamic_extent>(
          castData, in.size() * sizeof(T) / sizeof(U));
    } else {
      static_assert(Extent * sizeof(T) % sizeof(U) == 0);
      constexpr std::size_t kResSize = Extent * sizeof(T) / sizeof(U);
      return Span<U, kResSize>(castData, kResSize);
    }
  }
};

inline constexpr span_cast_impl_fn span_cast_impl;

} // namespace detail

/// static_span_cast
/// static_span_cast_fn
/// reinterpret_span_cast
/// reinterpret_span_cast_fn
/// const_span_cast
/// const_span_cast_fn
///
/// Casts a span to a different span. The result is a span referring to the same
/// region in memory but as a different type.
///
/// Example:
///
///   std::span<std::byte> bytes = ...
///   std::span<int> ints = folly::reinterpret_span_cast<int>(bytes);

template <typename U>
struct static_span_cast_fn {
  template <typename T, std::size_t Extent>
  constexpr auto operator()(detail::fallback_span::span<T, Extent> in) const {
    return detail::span_cast_impl(in, static_cast<U*>(in.data()));
  }
#if __cpp_lib_span >= 202002L
  template <typename T, std::size_t Extent>
  constexpr auto operator()(std::span<T, Extent> in) const {
    return detail::span_cast_impl(in, static_cast<U*>(in.data()));
  }
#endif
};
template <typename U>
inline constexpr static_span_cast_fn<U> static_span_cast;

template <typename U>
struct reinterpret_span_cast_fn {
  template <typename T, std::size_t Extent>
  constexpr auto operator()(detail::fallback_span::span<T, Extent> in) const {
    return detail::span_cast_impl(in, reinterpret_cast<U*>(in.data()));
  }
#if __cpp_lib_span >= 202002L
  template <typename T, std::size_t Extent>
  constexpr auto operator()(std::span<T, Extent> in) const {
    return detail::span_cast_impl(in, reinterpret_cast<U*>(in.data()));
  }
#endif
};
template <typename U>
inline constexpr reinterpret_span_cast_fn<U> reinterpret_span_cast;

template <typename U>
struct const_span_cast_fn {
  template <typename T, std::size_t Extent>
  constexpr auto operator()(detail::fallback_span::span<T, Extent> in) const {
    return detail::span_cast_impl(in, const_cast<U*>(in.data()));
  }
#if __cpp_lib_span >= 202002L
  template <typename T, std::size_t Extent>
  constexpr auto operator()(std::span<T, Extent> in) const {
    return detail::span_cast_impl(in, const_cast<U*>(in.data()));
  }
#endif
};
template <typename U>
inline constexpr const_span_cast_fn<U> const_span_cast;

namespace detail {

namespace fallback_span {

/// as_bytes
///
/// mimic: std::as_bytes, C++20
template <typename T, std::size_t Extent>
auto as_bytes(span<T, Extent> s) noexcept {
  return reinterpret_span_cast<std::byte const>(s);
}

/// as_writable_bytes
///
/// mimic: std::as_writable_bytes, C++20
template <
    typename T,
    std::size_t Extent,
    std::enable_if_t<!std::is_const_v<T>, int> = 0>
auto as_writable_bytes(span<T, Extent> s) noexcept {
  return reinterpret_span_cast<std::byte>(s);
}

} // namespace fallback_span

} // namespace detail

} // namespace folly
