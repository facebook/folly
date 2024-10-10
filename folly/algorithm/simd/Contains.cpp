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

#include <folly/algorithm/simd/Contains.h>

#include <algorithm>
#include <cstring>
#include <folly/algorithm/simd/detail/ContainsImpl.h>

namespace folly::simd::detail {

bool containsU8(
    folly::span<const std::uint8_t> haystack, std::uint8_t needle) noexcept {
  return containsImpl(haystack, needle);
}
bool containsU16(
    folly::span<const std::uint16_t> haystack, std::uint16_t needle) noexcept {
  return containsImpl(haystack, needle);
}
bool containsU32(
    folly::span<const std::uint32_t> haystack, std::uint32_t needle) noexcept {
  return containsImpl(haystack, needle);
}

bool containsU64(
    folly::span<const std::uint64_t> haystack, std::uint64_t needle) noexcept {
  return containsImpl(haystack, needle);
}

} // namespace folly::simd::detail
