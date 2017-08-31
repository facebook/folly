/*
 * Copyright 2017 Facebook, Inc.
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

#include <cstdint>
#include <cstring>
#include <type_traits>

namespace folly {

namespace detail {

template <typename Char>
constexpr size_t constexpr_strlen_internal(const Char* s, size_t len) {
  return *s == Char(0) ? len : constexpr_strlen_internal(s + 1, len + 1);
}
static_assert(
    constexpr_strlen_internal("123456789", 0) == 9,
    "Someone appears to have broken constexpr_strlen...");
} // namespace detail

template <typename Char>
constexpr size_t constexpr_strlen(const Char* s) {
  return detail::constexpr_strlen_internal(s, 0);
}

template <>
constexpr size_t constexpr_strlen(const char* s) {
#if defined(__clang__)
  return __builtin_strlen(s);
#elif defined(_MSC_VER) || defined(__CUDACC__)
  return detail::constexpr_strlen_internal(s, 0);
#else
  return std::strlen(s);
#endif
}
}
