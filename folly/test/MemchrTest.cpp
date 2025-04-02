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

#include <folly/FollyMemchr.h>

#include <folly/portability/GTest.h>

constexpr size_t kPageSize = 4096;
constexpr uint8_t kBufEnd = 0xDB;

// memset implementation test with 0xFF pattern
// buf must have an extra byte to be filled with magic constant
void testMemchrImpl(uint8_t* buf, size_t maxLen) {
  for (size_t len = 0; len < maxLen; len++) {
    for (size_t i = 0; i < maxLen; i++) {
      buf[i] = 0x0;
    }
    buf[len-1] = 0xFF;
    buf[len] = kBufEnd;
    auto* p = folly::memchr_long(buf, 0xFF, len);
    if (len > 0)
      EXPECT_EQ(buf+len-1, reinterpret_cast<uint8_t*>(p));
    else
      EXPECT_EQ(NULL, reinterpret_cast<uint8_t*>(p));
  }
}

TEST(MemchrAsmTest, alignedBuffer) {
  constexpr size_t kMaxSize = 2 * kPageSize;
  uint8_t* buf = reinterpret_cast<uint8_t*>(
      aligned_alloc(kPageSize, kMaxSize + 2 * kPageSize));
  // Get buffer aligned power of 2 from 16 all the way up to a page size
  for (size_t alignment = 16; alignment <= 32; alignment <<= 1) {
    testMemchrImpl(buf + (alignment % kPageSize), kMaxSize);
  }
  free(buf);
}

TEST(MemchrAsmTest, unalignedBuffer) {
  uint8_t* buf =
      reinterpret_cast<uint8_t*>(aligned_alloc(kPageSize, 2 * kPageSize));
  for (size_t alignment = 1; alignment <= 192; alignment++) {
    testMemchrImpl(buf + alignment, 256);
  }
  free(buf);
}
