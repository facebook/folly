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

#include <type_traits>

#include <folly/Portability.h>
#include <folly/Traits.h>
#include <folly/lang/SafeAssert.h>

namespace folly {

//  compiler_may_unsafely_assume
//
//  Unsafe. Avoid when not absolutely necessary.
//
//  Permits the compiler to assume the truth of the provided expression and to
//  optimize the surrounding code as if that expression were true.
//
//  Causes every possible kind of subtle, irreproducible, non-debuggable failure
//  if the provided expression is ever, for any reason, false: random crashes,
//  silent memory corruptions, arbitrary code execution, privacy violation, etc.
//
//  Must only be used on conditions that are provable internal logic invariants.
//
//  Must not be used on conditions which depend on external inputs. For such
//  cases, an assertion or exception may be used instead.
void compiler_may_unsafely_assume(bool cond);

//  compiler_may_unsafely_assume_unreachable
//
//  Unsafe. Avoid when not absolutely necessary.
//
//  Permits the compiler to assume that the given statement cannot be reached
//  and to optimize the surrounding code accordingly. Permits call sites to omit
//  return statements in non-void-returning functions without compiler error.
//
//  Causes every possible kind of subtle, irreproducible, non-debuggable failure
//  if the given statement is ever, for any reason, reached: random crashes,
//  silent memory corruptions, arbitrary code execution, privacy violation, etc.
//
//  Must only be used on conditions that are provable internal logic invariants.
//
//  Must not be used on conditions which depend on external inputs. For such
//  cases, an assertion or exception may be used instead.
[[noreturn]] void compiler_may_unsafely_assume_unreachable();

//  compiler_must_not_elide
//
//  Ensures that the referred-to value will be computed even when an optimizing
//  compiler might otherwise remove the computation. Note: this hint takes its
//  value paramaeter by reference.
//
//  Useful for values that are computed during benchmarking but otherwise are
//  unused. The compiler tends to do a good job at eliminating unused variables,
//  which can affect benchmark results, and this hint instructs the compiler to
//  treat the value as though it were used.
struct compiler_must_not_elide_fn {
  template <typename T>
  FOLLY_ALWAYS_INLINE void operator()(T const& t) const noexcept;
};
FOLLY_INLINE_VARIABLE constexpr compiler_must_not_elide_fn
    compiler_must_not_elide{};

//  compiler_must_not_predict
//
//  Ensures that the compiler will not use its knowledge of the referred-to
//  value to optimize or otherwise shape the following code. Note: this hint
//  takes its value parameter by reference.
//
//  Useful when constant propagation or power reduction is possible in a
//  benchmark but not in real use cases. Optimizations done to benchmarked code
//  which cannot be done to real code can affect benchmark results, and this
//  hint instructs the compiler to treat value which it can predict as though it
//  were unpredictable.
struct compiler_must_not_predict_fn {
  template <typename T>
  FOLLY_ALWAYS_INLINE void operator()(T& t) const noexcept;
};
FOLLY_INLINE_VARIABLE constexpr compiler_must_not_predict_fn
    compiler_must_not_predict{};

} // namespace folly

#include <folly/lang/Hint-inl.h>
