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

#include <folly/container/range_traits.h>

#include <array>
#include <list>
#include <string>
#include <string_view>
#include <vector>

#if __has_include(<span>)
#include <span>
#endif

#include <folly/portability/GTest.h>

struct RangeTraitsTest : testing::Test {};

TEST_F(RangeTraitsTest, is_contiguous_range_v) {
  enum some_enum {};

  EXPECT_FALSE((folly::is_contiguous_range_v<void>));
  EXPECT_FALSE((folly::is_contiguous_range_v<int>));
  EXPECT_FALSE((folly::is_contiguous_range_v<int&>));
  EXPECT_FALSE((folly::is_contiguous_range_v<int[]>));
  EXPECT_FALSE((folly::is_contiguous_range_v<int const[]>));
  EXPECT_FALSE((folly::is_contiguous_range_v<int*>));
  EXPECT_FALSE((folly::is_contiguous_range_v<int const*>));
  EXPECT_FALSE((folly::is_contiguous_range_v<int()>));
  EXPECT_FALSE((folly::is_contiguous_range_v<some_enum>));
  EXPECT_FALSE((folly::is_contiguous_range_v<std::list<int>>));

  EXPECT_TRUE((folly::is_contiguous_range_v<int[7]>));
  EXPECT_TRUE((folly::is_contiguous_range_v<int const[7]>));
  EXPECT_TRUE((folly::is_contiguous_range_v<std::array<int, 7>>));
  EXPECT_TRUE((folly::is_contiguous_range_v<std::string>));
  EXPECT_TRUE((folly::is_contiguous_range_v<std::string_view>));
  EXPECT_TRUE((folly::is_contiguous_range_v<std::vector<int>>));
#if __has_include(<span>)
  EXPECT_TRUE((folly::is_contiguous_range_v<std::span<int>>));
  EXPECT_TRUE((folly::is_contiguous_range_v<std::span<int const>>));
  EXPECT_TRUE((folly::is_contiguous_range_v<std::span<int, 7>>));
  EXPECT_TRUE((folly::is_contiguous_range_v<std::span<int const, 7>>));
#endif
}
