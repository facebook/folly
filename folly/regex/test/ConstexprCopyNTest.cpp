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

#include <folly/regex/detail/ConstexprCopyN.h>

#include <folly/portability/GTest.h>

using folly::regex::detail::constexpr_copy_n;

TEST(ConstexprCopyNTest, BasicIntCopy) {
  const int src[] = {1, 2, 3, 4, 5};
  int dest[5] = {};
  auto* end = constexpr_copy_n(src, 5, dest);
  EXPECT_EQ(end, dest + 5);
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(dest[i], src[i]);
  }
}

TEST(ConstexprCopyNTest, BasicCharCopy) {
  const char src[] = "hello";
  char dest[6] = {};
  auto* end = constexpr_copy_n(src, 5, dest);
  EXPECT_EQ(end, dest + 5);
  EXPECT_EQ(std::string_view(dest, 5), "hello");
}

TEST(ConstexprCopyNTest, ZeroCount) {
  const int src[] = {42};
  int dest[] = {0};
  auto* end = constexpr_copy_n(src, 0, dest);
  EXPECT_EQ(end, dest);
  EXPECT_EQ(dest[0], 0);
}

TEST(ConstexprCopyNTest, ConstexprEvaluation) {
  static constexpr auto result = [] {
    int src[] = {10, 20, 30};
    int dest[3] = {};
    constexpr_copy_n(src, 3, dest);
    return dest[0] + dest[1] + dest[2];
  }();
  static_assert(result == 60);
}

TEST(ConstexprCopyNTest, ReturnValueConstexpr) {
  static constexpr auto endOffset = [] {
    int src[] = {1, 2};
    int dest[2] = {};
    auto* end = constexpr_copy_n(src, 2, dest);
    return end - dest;
  }();
  static_assert(endOffset == 2);
}

TEST(ConstexprCopyNTest, Uint64Copy) {
  const uint64_t src[] = {0xDEADBEEF, 0xCAFEBABE, 0x12345678};
  uint64_t dest[3] = {};
  auto* end = constexpr_copy_n(src, 3, dest);
  EXPECT_EQ(end, dest + 3);
  EXPECT_EQ(dest[0], 0xDEADBEEF);
  EXPECT_EQ(dest[1], 0xCAFEBABE);
  EXPECT_EQ(dest[2], 0x12345678);
}
