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

#include <stdlib.h>
#include <cstddef>

#include <folly/FollyMemset.h>

#include <folly/portability/GTest.h>

constexpr size_t kPageSize = 4096;
constexpr uint8_t kBufEnd = 0xDB;

// memset implementation test with 0xFF pattern
// buf must have an extra byte to be filled with magic constant
void testMemsetImpl(uint8_t* buf, size_t maxLen) {
  for (size_t len = 0; len < maxLen; len++) {
    for (size_t i = 0; i < maxLen; i++) {
      buf[i] = 0x0;
    }
    buf[len] = kBufEnd;
    auto* p = folly::__folly_memset(buf, 0xFF, len);
    EXPECT_EQ(buf, reinterpret_cast<uint8_t*>(p));
    bool isEq = true;
    for (size_t i = 0; i < len; i++) {
      isEq &= buf[i] == 0xFF;
    }
    EXPECT_TRUE(isEq);
    EXPECT_EQ(buf[len], kBufEnd);
  }
}

TEST(MemsetAsmTest, alignedBuffer) {
  constexpr size_t kMaxSize = 2 * kPageSize;
  uint8_t* buf = reinterpret_cast<uint8_t*>(
      aligned_alloc(kPageSize, kMaxSize + 2 * kPageSize));
  // Get buffer aligned power of 2 from 16 all the way up to a page size
  for (size_t alignment = 16; alignment <= kPageSize; alignment <<= 1) {
    testMemsetImpl(buf + (alignment % kPageSize), kMaxSize);
  }
  free(buf);
}

TEST(MemsetAsmTest, unalignedBuffer) {
  uint8_t* buf =
      reinterpret_cast<uint8_t*>(aligned_alloc(kPageSize, 2 * kPageSize));
  for (size_t alignment = 1; alignment <= 192; alignment++) {
    testMemsetImpl(buf + alignment, 256);
  }
  free(buf);
}
