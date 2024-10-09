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

#include <folly/algorithm/simd/detail/ContainsImpl.h>

#include <folly/portability/GTest.h>

namespace folly::simd {

static_assert( //
    !std::is_invocable_v< //
        contains_fn,
        std::vector<double>&,
        double>);

static_assert( //
    std::is_invocable_v< //
        contains_fn,
        std::vector<int>&,
        int>);

template <typename T>
struct ContainsTest : ::testing::Test {};

using TypesToTest = ::testing::Types<
    std::int8_t,
    std::int16_t,
    std::int32_t,
    std::int64_t,
    std::uint8_t,
    std::uint16_t,
    std::uint32_t,
    std::uint64_t>;

TYPED_TEST_SUITE(ContainsTest, TypesToTest);

template <typename T>
void testSimdContainsVerify(folly::span<T> haystack, T needle, bool expected) {
  bool actual1 = simd::contains(haystack, needle);
  ASSERT_EQ(expected, actual1);

  auto const_haystack = folly::static_span_cast<const T>(haystack);

  if constexpr (
      std::is_same_v<T, std::uint8_t> || std::is_same_v<T, std::uint16_t> ||
      std::is_same_v<T, std::uint32_t> || std::is_same_v<T, std::uint64_t>) {
    bool actual2 = simd::detail::containsImplStd(const_haystack, needle);
    ASSERT_EQ(expected, actual2) << " haystack.size(): " << haystack.size();
  }

  if constexpr (simd::detail::hasHandwrittenContains<T>()) {
    bool actual3 =
        simd::detail::containsImplHandwritten(const_haystack, needle);
    ASSERT_EQ(expected, actual3) << " haystack.size(): " << haystack.size();
  }
}

TYPED_TEST(ContainsTest, Basic) {
  using T = TypeParam;

  for (std::size_t size = 0; size != 100; ++size) {
    std::vector<T> buf(size, T{0});
    for (std::size_t offset = 0; offset != std::min(32UL, buf.size());
         ++offset) {
      folly::span<T> haystack(buf.data() + offset, buf.data() + buf.size());
      T needle{1};
      ASSERT_NO_FATAL_FAILURE(
          testSimdContainsVerify(haystack, needle, /*expected*/ false));

      for (auto& x : haystack) {
        x = needle;
        ASSERT_NO_FATAL_FAILURE(
            testSimdContainsVerify(haystack, needle, /*expected*/ true));
        x = 0;
      }
    }
  }
}

} // namespace folly::simd
