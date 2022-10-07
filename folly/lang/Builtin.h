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

//  FOLLY_BUILTIN_UNPREDICTABLE
//
//  mimic: __builtin_unpredictable, clang
#if FOLLY_HAS_BUILTIN(__builtin_unpredictable)
#define FOLLY_BUILTIN_UNPREDICTABLE(exp) __builtin_unpredictable(exp)
#else
#define folly_builtin_unpredictable(exp) \
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

namespace folly {
namespace builtin {

namespace detail {

template <typename V>
struct predict_constinit_ {
  FOLLY_ERASE FOLLY_CONSTEVAL /* implicit */ predict_constinit_(
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
