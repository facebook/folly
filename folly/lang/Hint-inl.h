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

namespace folly {

FOLLY_ALWAYS_INLINE void compiler_may_unsafely_assume(bool cond) {
  FOLLY_SAFE_DCHECK(cond, "compiler-hint assumption fails at runtime");
#if defined(__clang__)
  __builtin_assume(cond);
#elif defined(__GNUC__)
  if (!cond) {
    __builtin_unreachable();
  }
#elif defined(_MSC_VER)
  __assume(cond);
#else
  while (!cond)
    ;
#endif
}

[[noreturn]] FOLLY_ALWAYS_INLINE void
compiler_may_unsafely_assume_unreachable() {
  FOLLY_SAFE_DCHECK(false, "compiler-hint unreachability reached at runtime");
#if defined(__GNUC__)
  __builtin_unreachable();
#elif defined(_MSC_VER)
  __assume(0);
#else
  while (!0)
    ;
#endif
}

#if defined(_MSC_VER) && !defined(__clang__)

namespace detail {

#pragma optimize("", off)

inline void compiler_must_force_sink(void const*) {}

#pragma optimize("", on)

} // namespace detail

template <typename T>
FOLLY_ALWAYS_INLINE void compiler_must_not_elide_fn::operator()(
    T const& t) const noexcept {
  detail::compiler_must_force_sink(&t);
}

template <typename T>
FOLLY_ALWAYS_INLINE void compiler_must_not_predict_fn::operator()(
    T& t) const noexcept {
  detail::compiler_must_force_sink(&t);
}

#else

namespace detail {

template <typename T, typename D = std::decay_t<T>>
using compiler_must_force_indirect = bool_constant<
    !is_trivially_copyable_v<D> || //
    sizeof(long) < sizeof(D) || //
    std::is_pointer<D>::value>;

template <typename T>
FOLLY_ALWAYS_INLINE void compiler_must_not_elide(T const& t, std::false_type) {
  // the "r" constraint forces the compiler to make the value available in a
  // register to the asm block, which means that it must first have been
  // computed or loaded
  //
  // used for small trivial values which the compiler will put into registers
  //
  // avoided for pointers to avoid fallout in calling code which mistakenly
  // applies the hint to the address of a value but not to the value itself
  asm volatile("" : : "r"(t));
}

template <typename T>
FOLLY_ALWAYS_INLINE void compiler_must_not_elide(T const& t, std::true_type) {
  // tells the compiler that the asm block will read the value from memory,
  // and that in addition it might read or write from any memory location
  //
  // if the memory clobber could be split into input and output, that would be
  // preferrable
  asm volatile("" : : "m"(t) : "memory");
}

template <typename T>
FOLLY_ALWAYS_INLINE void compiler_must_not_predict(T& t, std::false_type) {
  asm volatile("" : "+r"(t));
}

template <typename T>
FOLLY_ALWAYS_INLINE void compiler_must_not_predict(T& t, std::true_type) {
  asm volatile("" : : "m"(t) : "memory");
}

} // namespace detail

template <typename T>
FOLLY_ALWAYS_INLINE void compiler_must_not_elide_fn::operator()(
    T const& t) const noexcept {
  using i = detail::compiler_must_force_indirect<T>;
  detail::compiler_must_not_elide(t, i{});
}

template <typename T>
FOLLY_ALWAYS_INLINE void compiler_must_not_predict_fn::operator()(
    T& t) const noexcept {
  using i = detail::compiler_must_force_indirect<T>;
  detail::compiler_must_not_predict(t, i{});
}

#endif

} // namespace folly
