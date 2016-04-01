/*
 * Copyright 2016 Facebook, Inc.
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

namespace folly {

template <typename T>
constexpr T constexpr_max(T a, T b) {
  return a > b ? a : b;
}

template <typename T>
constexpr T constexpr_min(T a, T b) {
  return a < b ? a : b;
}

#ifdef _MSC_VER
constexpr size_t constexpr_strlen_internal(const char* s, size_t len) {
  return *s == '\0' ? len : constexpr_strlen_internal(s + 1, len + 1);
}
static_assert(constexpr_strlen_internal("123456789", 0) == 9,
              "Someone appears to have broken constexpr_strlen...");
#endif

constexpr size_t constexpr_strlen(const char* s) {
#if defined(__clang__)
  return __builtin_strlen(s);
#elif defined(_MSC_VER)
  return s == nullptr ? 0 : constexpr_strlen_internal(s, 0);
#else
  return std::strlen(s);
#endif
}
}
