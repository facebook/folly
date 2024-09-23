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

#include <folly/algorithm/simd/detail/SimdContainsImpl.h>

#include <folly/portability/GTest.h>

namespace folly {

struct SimdContainsTest : ::testing::Test {};

template <typename T>
void testSimdContainsVerify(std::span<T> haystack, T needle, bool expected) {
  bool actual1 = simd_contains(haystack, needle);
  ASSERT_EQ(expected, actual1);

  auto const_haystack = folly::static_span_cast<const T>(haystack);

  if constexpr (
      std::is_same_v<T, std::uint8_t> || std::is_same_v<T, std::uint16_t> ||
      std::is_same_v<T, std::uint32_t> || std::is_same_v<T, std::uint64_t>) {
    bool actual2 = simd_detail::simdContainsImplStd(const_haystack, needle);
    ASSERT_EQ(expected, actual2) << " haystack.size(): " << haystack.size();
  }

  if constexpr (std::is_same_v<T, std::uint8_t>) {
    bool actual3 =
        simd_detail::simdContainsImplHandwritten(const_haystack, needle);
    ASSERT_EQ(expected, actual3) << " haystack.size(): " << haystack.size();
  }
}

template <typename T>
void testSimdContains() {
  for (std::size_t size = 0; size != 100; ++size) {
    std::vector<T> buf(size, T{0});
    for (std::size_t offset = 0; offset != std::min(32UL, buf.size());
         ++offset) {
      folly::span searching(buf.begin() + offset, buf.end());
      T needle{1};
      testSimdContainsVerify(searching, needle, /*expected*/ false);

      for (auto& x : searching) {
        x = needle;
        testSimdContainsVerify(searching, needle, /*expected*/ true);
        x = 0;
      }
    }
  }
}

TEST_F(SimdContainsTest, AllTypes) {
  testSimdContains<std::int8_t>();
  testSimdContains<std::int16_t>();
  testSimdContains<std::int32_t>();
  testSimdContains<std::int64_t>();
  testSimdContains<std::uint8_t>();
  testSimdContains<std::uint16_t>();
  testSimdContains<std::uint32_t>();
  testSimdContains<std::uint64_t>();
}

} // namespace folly
