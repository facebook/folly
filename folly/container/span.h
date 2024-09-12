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

#include <cassert>
#include <cstddef>

#include <folly/Portability.h>
#include <folly/portability/Constexpr.h>

#if __cpp_lib_span >= 202002L
#include <span>
#endif

namespace folly {

#if __cpp_lib_span >= 202002L

namespace detail {

struct span_cast_impl_fn {
  template <typename U, typename T, std::size_t Extent>
  constexpr auto operator()(std::span<T, Extent> in, U* castData) const {
    assert(
        static_cast<void const*>(in.data()) ==
        static_cast<void const*>(castData));

    // check alignment
    if (!folly::is_constant_evaluated_or(true)) {
      assert(reinterpret_cast<std::uintptr_t>(in.data()) % sizeof(U) == 0);
    }

    if constexpr (Extent == std::dynamic_extent) {
      assert(in.size() * sizeof(T) % sizeof(U) == 0);
      return std::span<U>(castData, in.size() * sizeof(T) / sizeof(U));
    } else {
      static_assert(Extent * sizeof(T) % sizeof(U) == 0);
      constexpr std::size_t kResSize = Extent * sizeof(T) / sizeof(U);
      return std::span<U, kResSize>(castData, kResSize);
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
  constexpr auto operator()(std::span<T, Extent> in) const {
    return detail::span_cast_impl(in, static_cast<U*>(in.data()));
  }
};
template <typename U>
inline constexpr static_span_cast_fn<U> static_span_cast;

template <typename U>
struct reinterpret_span_cast_fn {
  template <typename T, std::size_t Extent>
  constexpr auto operator()(std::span<T, Extent> in) const {
    return detail::span_cast_impl(in, reinterpret_cast<U*>(in.data()));
  }
};
template <typename U>
inline constexpr reinterpret_span_cast_fn<U> reinterpret_span_cast;

template <typename U>
struct const_span_cast_fn {
  template <typename T, std::size_t Extent>
  constexpr auto operator()(std::span<T, Extent> in) const {
    return detail::span_cast_impl(in, const_cast<U*>(in.data()));
  }
};
template <typename U>
inline constexpr const_span_cast_fn<U> const_span_cast;

#endif

} // namespace folly
