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

#include <algorithm>
#include <cstring>
#include <cwchar>
#include <type_traits>

#include <folly/CPortability.h>
#include <folly/algorithm/simd/detail/SimdAnyOf.h>
#include <folly/algorithm/simd/detail/SimdPlatform.h>
#include <folly/container/span.h>

namespace folly::simd::detail {

/*
 * The functions in this file are FOLLY_ERASE to make sure
 * that the only place behind a call boundary is the explicit one.
 */

template <typename T>
FOLLY_ALWAYS_INLINE bool containsImplStd(
    folly::span<const T> haystack, T needle) {
  static_assert(
      std::is_unsigned_v<T>, "we should only get here for uint8/16/32/64");
  if constexpr (sizeof(T) == 1) {
    auto* ptr = reinterpret_cast<const char*>(haystack.data());
    auto castNeedle = static_cast<char>(needle);
    if (haystack.empty()) { // memchr requires not null
      return false;
    }
    return std::memchr(ptr, castNeedle, haystack.size()) != nullptr;
  } else if constexpr (sizeof(T) == sizeof(wchar_t)) {
    auto* ptr = reinterpret_cast<const wchar_t*>(haystack.data());
    auto castNeedle = static_cast<wchar_t>(needle);
    if (haystack.empty()) { // wmemchr requires not null
      return false;
    }
    return std::wmemchr(ptr, castNeedle, haystack.size()) != nullptr;
  } else {
    // Using find instead of any_of on an off chance that the standard library
    // will add some custom vectorization.
    // That wouldn't be possible for any_of because of the predicates.
    return std::find(haystack.begin(), haystack.end(), needle) !=
        haystack.end();
  }
}

template <typename T>
constexpr bool hasHandwrittenContains() {
  return !std::is_same_v<SimdPlatform<T>, void> &&
      (std::is_same_v<std::uint8_t, T> || std::is_same_v<std::uint16_t, T> ||
       std::is_same_v<std::uint32_t, T> || std::is_same_v<std::uint64_t, T>);
}

template <typename T, typename Platform = SimdPlatform<T>>
FOLLY_ALWAYS_INLINE bool containsImplHandwritten(
    folly::span<const T> haystack, T needle) {
  static_assert(!std::is_same_v<Platform, void>, "");
  return simdAnyOf<Platform, 4>(
      haystack.data(),
      haystack.data() + haystack.size(),
      [&](typename Platform::reg_t x) {
        return Platform::equal(x, static_cast<T>(needle));
      });
}

template <typename T>
FOLLY_ALWAYS_INLINE bool containsImpl(folly::span<const T> haystack, T needle) {
  if constexpr (hasHandwrittenContains<T>()) {
    return containsImplHandwritten(haystack, needle);
  } else {
    return containsImplStd(haystack, needle);
  }
}

} // namespace folly::simd::detail
