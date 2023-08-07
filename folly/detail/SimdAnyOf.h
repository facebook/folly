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

#include <folly/CPortability.h>
#include <folly/detail/SimdForEach.h>
#include <folly/detail/UnrollUtils.h>

namespace folly {
namespace simd_detail {

/**
 * AnyOfDelegate
 *
 * Implementation detail of simdAnyOf
 * This is a delegate to simdForEach
 *
 * Based on
 * https://github.com/jfalcou/eve/blob/9309d6d17a35004adb371099d79082c8cc75d3a6/include/eve/module/algo/algo/any_of.hpp#L23
 */
template <typename Platform, typename I, typename P>
struct AnyOfDelegate {
  // _p to deal with a shadow warning on an old gcc
  explicit AnyOfDelegate(P _p) : p(_p) {}

  template <typename Ignore, typename UnrollStep>
  FOLLY_ALWAYS_INLINE bool step(I it, Ignore ignore, UnrollStep) {
    auto test = p(Platform::loada(it, ignore));
    res = Platform::any(test, ignore);
    return res;
  }

  template <std::size_t N>
  FOLLY_ALWAYS_INLINE bool unrolledStep(std::array<I, N> arr) {
    // Don't have to forceinline - no user code dependency
    auto loaded = detail::UnrollUtils::arrayMap(
        arr, [](I it) { return Platform::loada(it, ignore_none{}); });
    auto tests = detail::UnrollUtils::arrayMap(loaded, p);
    auto test = detail::UnrollUtils::arrayReduce(tests, Platform::logical_or);
    res = Platform::any(test, ignore_none{});
    return res;
  }

  P p;
  bool res = false;
};

/**
 * simdAnyOf<Platform, unrolling = 4>(f, l, p);
 *
 * Like std::any_of but with vectorized predicates.
 * Predicate shoud accept Platform::reg_t and return Platform::logical_t.
 *
 * By default is unrolled 4 ways but for expensive predicates you might want to
 * use an unroll factor of 1.
 *
 * Function is marked as FOLLY_ALWAYS_INLINE because we don't want the end users
 * to include this directly, we want to implement a specific function and hide
 * that code behind a compile time boundary.
 */
template <typename Platform, int unrolling = 4, typename T, typename P>
FOLLY_ALWAYS_INLINE bool simdAnyOf(T* f, T* l, P p) {
  AnyOfDelegate<Platform, T*, P> delegate{p};
  simdForEachAligning<unrolling>(Platform::kCardinal, f, l, delegate);
  return delegate.res;
}

} // namespace simd_detail
} // namespace folly
