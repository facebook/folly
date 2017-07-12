/*
 * Copyright 2017-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <folly/Portability.h>

namespace folly {

#ifdef __GNUC__
#ifdef __has_builtin
#if __has_builtin(__builtin_launder)
#define FOLLY_USE_BUILTIN_LAUNDER 1
#endif
#endif
#endif

/**
 * Approximate backport from C++17 of std::launder. It should be `constexpr`
 * but that can't be done without specific support from the compiler.
 */
template <typename T>
FOLLY_NODISCARD inline T* launder(T* in) noexcept {
#ifdef __GNUC__
#ifdef FOLLY_USE_BUILTIN_LAUNDER
  // Newer GCC versions have a builtin for this with no unwanted side-effects
  return __builtin_launder(in);
#else
  // This inline assembler block declares that `in` is an input and an output,
  // so the compiler has to assume that it has been changed inside the block.
  __asm__("" : "+r"(in));
  return in;
#endif
#else
  static_assert(
      false, "folly::launder is not implemented for this environment");
#endif
}

#ifdef FOLLY_USE_BUILTIN_LAUNDER
#undef FOLLY_USE_BUILTIN_LAUNDER
#endif

/* The standard explicitly forbids laundering these */
void launder(void*) = delete;
void launder(void const*) = delete;
void launder(void volatile*) = delete;
void launder(void const volatile*) = delete;
template <typename T, typename... Args>
void launder(T (*)(Args...)) = delete;
} // namespace folly
