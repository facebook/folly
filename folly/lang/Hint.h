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

//  compiler_may_unsafely_assume_separate_storage
//
//  Unsafe. Avoid when not absolutely necessary.
//
//  Permits the compiler to assume that the given pointers point into regions
//  that were allocated separately, and that therefore any pointers at any
//  offset relative to one is never equal to any pointer at any offset relative
//  to the other.
//
//  Regions being allocated separately means that they are separate global
//  variables, thread-local variables, stack variables, or heap allocations.
//
//  This can be helpful when some library has an ownership model that is opaque
//  to the compiler. For example, modifying some_vector[0] never modifies the
//  vector itself; changes to parser state don't need to be stored to memory
//  before reading the next byte of data to be parsed, etc.
void compiler_may_unsafely_assume_separate_storage(
    const void* a, const void* b);

//  compiler_must_not_elide
//
//  Ensures that the referred-to value will be computed even when an optimizing
//  compiler might otherwise remove the computation. Note: this hint takes its
//  value parameter by reference.
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

//  ----

namespace detail {

template <typename T>
using detect_folly_is_unsafe_for_async_usage =
    typename T::folly_is_unsafe_for_async_usage;

//  is_unsafe_for_async_usage_v
//
//  Whether a type is directly marked as unsafe for async usage with a member
//  type alias. See unsafe_for_async_usage below.
template <typename T>
FOLLY_INLINE_VARIABLE constexpr bool is_unsafe_for_async_usage_v =
    detected_or_t<
        std::false_type,
        detail::detect_folly_is_unsafe_for_async_usage,
        T>::value;

} // namespace detail

//  unsafe_for_async_usage
//
//  Defines member type alias folly_is_unsafe_for_async_usage, which is the tag
//  marking the class as unsafe for async usage. Serves as a convenience wrapper
//  around the tag.
//
//  The meaning of the tag is that the current thread must not be yielded in any
//  way during the lifetime of any tagged object.
//
//  The most common form of yielding the current thread during the lifetime of
//  an object is where the lifetime of the object crosses a coroutine suspension
//  point. Example:
//
//    struct unsafe : private unsafe_for_async_usage {};
//    Task<> sink();
//    Task<> go() {
//      unsafe object; // object is tagged as unsafe for async usage
//      co_await sink(); // but crossing the co_await is async usage
//    }
//
//  Some objects, especially some which are intended for use with the RAII
//  pattern, may be thread-sensitive and permit access only on one single thread
//  throughout their lifetimes. Or they may be non-reentrant and may permit only
//  well-nested accesses or may not permit any reentrant accesses at all. In
//  either case, they do not permit yielding the current thread during their
//  lifetimes. Examples:
//  * Some objects that rely internally on thread-locals.
//    * In particular, the storage defined by FOLLY_DECLARE_REUSED.
//  * lock_guards for most mutexes.
//  * Some advanced concurrency primitives.
//    * In particular, hazard-pointers and rcu-guards.
//
//  Note that the current thread being yielded refers to any form of cooperative
//  multitasking, such as coroutines, as compared with the kernel preempting the
//  current thread and then resuming it later.
//
//  In order to mark a class type, either:
//  * Declare a member type alias
//    `using folly_is_unsafe_for_async_usage = std::true_type`.
//  * Declare either a non-static data member or a base which is marked. As a
//    convenience, unsafe_for_async_usage is marked and may be used as that non-
//    static data member or base. If using a non-static data member, it is ideal
//    to declare it with attribute [[no_unique_address]], possibly as wrapped in
//    FOLLY_ATTR_NO_UNIQUE_ADDRESS, to avoid increasing the size of the class.
//  * std::lock_guard/unique_lock/scoped_lock are considered unsafe unless the
//    underlying mutex has a typedef `folly_coro_aware_mutex`.
//
// NOTE: you can explicitly opt out from a check for a type by having
//       `folly_is_unsafe_for_async_usage` be `std::false_type`.
//
//  It is recommended to use a non-static data member or a base which is marked,
//  in preference to using a member type alias, since it is impossible to typo
//  the spelling of a non-static data member or base while it is easy to typo
//  the spelling of a member type alias.
//
//  Example:
//
//    struct thread_counter {
//      static thread_local int value;
//    };
//    struct thread_counter_raii : private unsafe_for_async_usage {
//      thread_counter_raii() { ++thread_counter::value; }
//      ~thread_counter_raii() { --thread_counter::value; }
//    };
//    coroutine callee();
//    coroutine caller() {
//      thread_counter_raii g;
//      co_await callee(); // static analysis might warn here
//    }
//
//  This marker can be used to implement a static analysis that detects misuses
//  with objects of classes marked with this tag, namely, where the current
//  thread may be yielded in any way during the lifetimes of such objects.
struct unsafe_for_async_usage { // a convenience wrapper for the marker below:
  // the marker member type alias
  using folly_is_unsafe_for_async_usage = std::true_type;
};
static_assert(detail::is_unsafe_for_async_usage_v<unsafe_for_async_usage>, "");

namespace detail {

template <typename T>
using detect_folly_is_coro_aware_mutex = typename T::folly_coro_aware_mutex;

} // namespace detail

// Inheriting or having a member `unsafe_for_async_usage_if` will conditionally
// tag the type.
template <bool If>
struct unsafe_for_async_usage_if {};

template <>
struct unsafe_for_async_usage_if<true> {
  using folly_is_unsafe_for_async_usage = std::true_type;
};

// Detects the presense of folly_coro_aware_mutex nested typedef.
// This helps custom lock guards have the same behavior as std::lock_guard.
template <typename T>
constexpr bool is_coro_aware_mutex_v =
    is_detected_v<detail::detect_folly_is_coro_aware_mutex, T>;

} // namespace folly

#include <folly/lang/Hint-inl.h>
