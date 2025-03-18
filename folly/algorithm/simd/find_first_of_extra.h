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

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <execution>

#include <folly/Range.h>
#include <folly/container/span.h>

namespace folly::simd {

namespace detail {

#if __cpp_lib_execution >= 201902L

/// stdfind_vector_finder_first_of
///
/// A find-first-of finder which simply wraps std::find_first_of with an
/// execution policy.
///
/// Like stdfind_scalar_finder_first_of, but potentially accelerated. Depends
/// on the implementation.
///
/// Requires no precomputation or storage.
///
/// Extracted to a separate facility since this has high startup cost and to
/// isolate the dependency on tbb which libstdc++ brings.
///
/// Only supports positive-match (find-first-of) and not negative-match
/// (find-first-not-of) since std::find
template <typename CharT>
class stdfind_vector_finder_first_of {
 private:
  using value_type = CharT;
  using view = span<CharT const>;

  alignas(sizeof(view)) view const alphabet_;

 public:
  constexpr explicit stdfind_vector_finder_first_of(
      view const alphabet) noexcept
      : alphabet_{alphabet} {}

  size_t operator()(view const input, size_t const pos = 0) const noexcept {
    auto const r = std::find_first_of(
        std::execution::unseq,
        input.subspan(pos).begin(),
        input.end(),
        alphabet_.begin(),
        alphabet_.end());
    return r - input.begin();
  }
};

#endif

/// rngfind_vector_finder_first_of
///
/// A find-first-of finder which simply wraps folly::Range::find_first_of. This
/// algorithm has its own internal acceleration.
///
/// Requires no precomputation or storage.
///
/// Implemented for x86-64 architecture.
///
/// Extracted to a separate facility since this has high startup cost.
///
/// Only supports positive-match (find-first-of) and not negative-match
/// (find-first-not-of) since std::find
template <typename CharT>
class rngfind_vector_finder_first_of {
 private:
  using value_type = CharT;
  using view = span<CharT const>;

  alignas(sizeof(view)) view const alphabet_;

 public:
  constexpr explicit rngfind_vector_finder_first_of(
      view const alphabet) noexcept
      : alphabet_{alphabet} {}

  size_t operator()(view const input, size_t const pos = 0) const noexcept {
    auto const r = crange(input).find_first_of(crange(alphabet_), pos);
    return r == size_t(-1) ? input.size() : r;
  }
};

} // namespace detail

#if __cpp_lib_execution >= 201902L

template <typename CharT>
using basic_stdfind_vector_finder_first_of =
    detail::stdfind_vector_finder_first_of<CharT>;

using stdfind_vector_finder_first_of =
    basic_stdfind_vector_finder_first_of<char>;

#endif

template <typename CharT>
using basic_rngfind_vector_finder_first_of =
    detail::rngfind_vector_finder_first_of<CharT>;

using rngfind_vector_finder_first_of =
    basic_rngfind_vector_finder_first_of<char>;

} // namespace folly::simd
