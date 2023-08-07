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

#include <folly/container/detail/F14Table.h>

#include <folly/portability/GTest.h>

#include <limits>

TEST(F14SizeAndChunkShift, packed) {
  folly::f14::detail::SizeAndChunkShift sz;
  static_assert(sizeof(sz) == sizeof(size_t));
  EXPECT_EQ(sz.size(), 0);
  EXPECT_EQ(sz.chunkShift(), 0);

  sz.setSize(12345678);
  EXPECT_EQ(sz.size(), 12345678);
  EXPECT_EQ(sz.chunkShift(), 0);

  sz.setChunkCount(1);
  EXPECT_EQ(sz.size(), 12345678);
  EXPECT_EQ(sz.chunkCount(), 1);
  EXPECT_EQ(sz.chunkShift(), 0);

  for (int shift = 0;
       shift <= folly::f14::detail::SizeAndChunkShift::kMaxSupportedChunkShift;
       ++shift) {
    const auto count = (1ULL << shift);
    sz.setChunkCount(count);
    EXPECT_EQ(sz.size(), 12345678);
    EXPECT_EQ(sz.chunkCount(), count);
    EXPECT_EQ(sz.chunkShift(), shift);
  }
}
