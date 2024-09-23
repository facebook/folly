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

#include <folly/algorithm/simd/SimdContains.h>

#include <algorithm>
#include <cstring>
#include <folly/algorithm/simd/detail/SimdContainsImpl.h>

namespace folly::simd_detail {

bool simdContainsU8(
    folly::span<const std::uint8_t> haystack, std::uint8_t needle) {
  return simdContainsImpl(haystack, needle);
}
bool simdContainsU16(
    folly::span<const std::uint16_t> haystack, std::uint16_t needle) {
  return simdContainsImpl(haystack, needle);
}
bool simdContainsU32(
    folly::span<const std::uint32_t> haystack, std::uint32_t needle) {
  return simdContainsImpl(haystack, needle);
}

bool simdContainsU64(
    folly::span<const std::uint64_t> haystack, std::uint64_t needle) {
  return simdContainsImpl(haystack, needle);
}

} // namespace folly::simd_detail
