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

#include <folly/lang/Align.h>

#include <folly/portability/GTest.h>

#include <array>

namespace folly {

TEST(Align, AlignFloor) {
  static_assert(1 == folly::align_floor(1, 1));
  static_assert(2 == folly::align_floor(3, 2));
  static_assert(8 == folly::align_floor(9, 8));
  static_assert(4096 * 2 == folly::align_floor(4096 * 3 - 1, 4096));

  alignas(64) std::array<char, 256> alignedBuf;
  auto* alignedPtr = alignedBuf.data();

  {
    EXPECT_EQ(alignedPtr, folly::align_floor(alignedPtr + 15, 16));
    EXPECT_EQ(alignedPtr + 8, folly::align_floor(alignedPtr + 15, 8));
    EXPECT_EQ(alignedPtr, folly::align_floor(alignedPtr + 8, 64));
  }

  // not char ptrs
  {
    auto* alignedPtrInt = reinterpret_cast<const int*>(alignedPtr);
    EXPECT_EQ(alignedPtrInt, folly::align_floor(alignedPtrInt + 2, 16));
    EXPECT_EQ(alignedPtrInt + 2, folly::align_floor(alignedPtrInt + 2, 4));
  }

  // void* ptrs
  {
    EXPECT_EQ(
        static_cast<const void*>(alignedPtr + 8),
        folly::align_floor(static_cast<const void*>(alignedPtr + 15), 8));
  }
}

TEST(Align, AlignCeil) {
  static_assert(1 == folly::align_ceil(1, 1));
  static_assert(2 == folly::align_ceil(2, 2));
  static_assert(4 == folly::align_ceil(3, 2));
  static_assert(8 == folly::align_ceil(8, 8));
  static_assert(16 == folly::align_ceil(9, 8));
  static_assert(4096 * 3 == folly::align_ceil(4096 * 3 - 1, 4096));

  alignas(64) std::array<char, 256> alignedBuf;
  auto* alignedPtr = alignedBuf.data();

  {
    EXPECT_EQ(alignedPtr, folly::align_ceil(alignedPtr, 16));
    EXPECT_EQ(alignedPtr + 16, folly::align_ceil(alignedPtr + 15, 16));
    EXPECT_EQ(alignedPtr + 32, folly::align_ceil(alignedPtr + 7, 32));
  }

  // not char ptrs
  {
    auto* alignedPtrInt = reinterpret_cast<const int*>(alignedPtr);
    EXPECT_EQ(alignedPtrInt + 4, folly::align_ceil(alignedPtrInt + 2, 16));
    EXPECT_EQ(alignedPtrInt + 2, folly::align_ceil(alignedPtrInt + 2, 4));
  }

  // void* ptrs
  {
    EXPECT_EQ(
        static_cast<const void*>(alignedPtr + 16),
        folly::align_ceil(static_cast<const void*>(alignedPtr + 15), 8));
  }
}

} // namespace folly
