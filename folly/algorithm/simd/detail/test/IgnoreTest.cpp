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

#include <folly/algorithm/simd/detail/Ignore.h>

#include <cstdint>

#include <folly/portability/GTest.h>

namespace folly::simd::detail {

struct IgnoreTest : ::testing::Test {};

TEST_F(IgnoreTest, MaskClearIgnored) {
  auto mmask =
      std::pair{std::uint8_t{0xff}, std::integral_constant<std::uint32_t, 2>{}};

  // mostly relying on folly::clear_<>_n_bits working correctly
  // simd any of also covers a lot of cases.
  // this is just the bare minimal smoke test.

  mmaskClearIgnored<4>(mmask, ignore_none{});
  EXPECT_EQ(0xff, mmask.first);

  mmaskClearIgnored<4>(mmask, ignore_extrema{1, 2});
  EXPECT_EQ(0b0000'1100, mmask.first);

  mmaskClearIgnored<2>(mmask, ignore_extrema{0, 1});
  EXPECT_EQ(0b0000'0000, mmask.first);
}

} // namespace folly::simd::detail
