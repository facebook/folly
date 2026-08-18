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

#include <folly/Portability.h>

#if !FOLLY_HAS_BUILTIN(__builtin_prefetch)
#if FOLLY_NEON
#include <intrin.h>
#elif FOLLY_SSE >= 1
#include <xmmintrin.h>
#endif
#endif

//  FOLLY_BUILTIN_UNPREDICTABLE
//
//  mimic: __builtin_unpredictable, clang
#if FOLLY_HAS_BUILTIN(__builtin_unpredictable)
#define FOLLY_BUILTIN_UNPREDICTABLE(exp) __builtin_unpredictable(exp)
#else
#define FOLLY_BUILTIN_UNPREDICTABLE(exp) \
  ::folly::builtin::detail::predict_<long long>(exp)
#endif

//  FOLLY_BUILTIN_EXPECT
//
//  mimic: __builtin_expect, gcc/clang
#if FOLLY_HAS_BUILTIN(__builtin_expect)
#define FOLLY_BUILTIN_EXPECT(exp, c) __builtin_expect(static_cast<bool>(exp), c)
#else
#define FOLLY_BUILTIN_EXPECT(exp, c) \
  ::folly::builtin::detail::predict_<long>(exp, c)
#endif

//  FOLLY_BUILTIN_EXPECT_WITH_PROBABILITY
//
//  mimic: __builtin_expect_with_probability, gcc/clang
#if FOLLY_HAS_BUILTIN(__builtin_expect_with_probability)
#define FOLLY_BUILTIN_EXPECT_WITH_PROBABILITY(exp, c, p) \
  __builtin_expect_with_probability(exp, c, p)
#else
#define FOLLY_BUILTIN_EXPECT_WITH_PROBABILITY(exp, c, p) \
  ::folly::builtin::detail::predict_<long>(exp, c, p)
#endif

//  FOLLY_BUILTIN_PREFETCH
//
//  Issues a hardware prefetch for the cache line containing addr.
//
//  rw and locality carry the same meanings as in __builtin_prefetch, and like
//  its parameters they must be constant expressions: rw is 0 for a read or 1
//  for a write, and locality runs from 0 (none) to 3 (high).
//
//  Purely a hint: has no effect other than on timing, and never faults, so
//  addr may be wild (unmapped or inaccessible). Callers rely on the latter to
//  prefetch speculatively computed addresses without first bounds-checking
//  them.
//
//  Calculation of the address must itself be valid, i.e. must not exhibit
//  undefined behavior, regardless of whether or not the resulting address is
//  valid.
//
//  mimic: __builtin_prefetch, gcc/clang
#define FOLLY_BUILTIN_PREFETCH(addr, rw, locality) \
  ::folly::builtin::detail::prefetch_<(rw), (locality)>(addr)

namespace folly {
namespace builtin {

namespace detail {

//  rw and locality are template parameters rather than function parameters
//  because __builtin_prefetch and _mm_prefetch both require them to be
//  constant expressions.
template <int Rw, int Locality>
FOLLY_ERASE void prefetch_(void const* addr) noexcept {
  static_assert(Rw == 0 || Rw == 1, "rw must be 0 (read) or 1 (write)");
  static_assert(Locality >= 0 && Locality <= 3, "locality must be 0 through 3");
#if FOLLY_HAS_BUILTIN(__builtin_prefetch)
  //  addr is permitted to be wild, which -Warray-bounds may flag when the
  //  caller computes it from an out-of-range index.
  FOLLY_PUSH_WARNING
  FOLLY_GNU_DISABLE_WARNING("-Warray-bounds")
  __builtin_prefetch(addr, Rw, Locality);
  FOLLY_POP_WARNING
#elif FOLLY_NEON
  //  __prefetch takes neither hint; both are necessarily dropped here.
  __prefetch(addr);
#elif FOLLY_SSE >= 1
  //  _mm_prefetch takes no rw hint, so only locality survives the mapping.
  constexpr auto hint[] = {
      _MM_HINT_NTA,
      _MM_HINT_T2,
      _MM_HINT_T1,
      _MM_HINT_T0,
  };
  _mm_prefetch(static_cast<char const*>(addr), hint[Locality]);
#else
  (void)addr;
#endif
}

template <typename V>
struct predict_constinit_ {
  FOLLY_ERASE consteval /* implicit */ predict_constinit_(
      V /* anonymous */) noexcept {}
};

template <typename E>
FOLLY_ERASE constexpr E predict_(
    E exp,
    predict_constinit_<long> /* anonymous */ = 0,
    predict_constinit_<double> /* anonymous */ = 0.) {
  return exp;
}

} // namespace detail

} // namespace builtin
} // namespace folly
