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

#include <cstddef>

namespace folly::regex::detail {

//  constexpr_copy_n
//
//  constexpr-compatible element-wise copy. Uses __builtin_memcpy when
//  available as a constexpr builtin (dramatically faster in constexpr
//  evaluation), otherwise falls back to a trivial element-by-element loop.
//
//  Returns dest + count (past-the-end output pointer), matching
//  std::copy_n semantics.
template <typename T>
constexpr T* constexpr_copy_n(
    const T* src, std::size_t count, T* dest) noexcept {
#if defined(__has_constexpr_builtin)
#if __has_constexpr_builtin(__builtin_memcpy)
  __builtin_memcpy(dest, src, count * sizeof(T));
#else
  for (std::size_t i = 0; i < count; ++i) {
    dest[i] = src[i];
  }
#endif
#else
  for (std::size_t i = 0; i < count; ++i) {
    dest[i] = src[i];
  }
#endif
  return dest + count;
}

} // namespace folly::regex::detail
