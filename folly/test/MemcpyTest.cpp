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

#include <array>

#include <folly/FollyMemcpy.h>
#include <folly/Portability.h>
#include <folly/portability/GTest.h>

namespace {

constexpr size_t kSize = 4096 * 4;
constexpr size_t kBufSize = 4096 * 50;
std::array<char, kBufSize> src;
std::array<char, kBufSize> dst;

char expected_src(size_t offset) {
  return static_cast<char>(offset % 128);
}

char expected_dst(size_t offset) {
  // Don't use any common characters with src.
  return static_cast<char>((offset % 128) + 128);
}

void init(size_t size) {
  for (size_t i = 0; i < size; ++i) {
    src[i] = expected_src(i);
    dst[i] = expected_dst(i);
  }
}
} // namespace

TEST(follyMemcpy, zeroLen)
FOLLY_DISABLE_UNDEFINED_BEHAVIOR_SANITIZER("nonnull-attribute") {
  // If length is 0, we shouldn't touch any memory.  So this should
  // not crash.
  char* srcNull = nullptr;
  char* dstNull = nullptr;
  folly::__folly_memcpy(dstNull, srcNull, 0);
}

// Test copy `len' bytes and verify that exactly `len' bytes are copied.
void testLen(size_t len, size_t dst_offset = 0, size_t src_offset = 0) {
  if (len + std::max(dst_offset, src_offset) + 1 > kBufSize) {
    return;
  }
  init(len + std::max(dst_offset, src_offset) + 1);
  void* ret = folly::__folly_memcpy(
      dst.data() + dst_offset, src.data() + src_offset, len);
  ASSERT_EQ(ret, dst.data() + dst_offset);
  for (size_t i = 0; i < len; ++i) {
    ASSERT_EQ(src[i + src_offset], expected_src(i + src_offset))
        << "__folly_memcpy(dst+" << dst_offset << ", src+" << src_offset << ", "
        << len << "), i = " << i;
    ASSERT_EQ(src[i + src_offset], dst[i + dst_offset])
        << "__folly_memcpy(dst+" << dst_offset << ", src+" << src_offset << ", "
        << len << "), i = " << i;
  }
  if (len + dst_offset < kBufSize) {
    ASSERT_EQ(dst[len + dst_offset], expected_dst(len + dst_offset))
        << "__folly_memcpy(dst+" << dst_offset << ", src+" << src_offset << ", "
        << len << "), overwrote";
  }
  if (src_offset > 0) {
    ASSERT_EQ(src[src_offset - 1], expected_src(src_offset - 1));
  }
  if (dst_offset > 0) {
    ASSERT_EQ(dst[dst_offset - 1], expected_dst(dst_offset - 1));
  }
}

TEST(follyMemcpy, small) {
  for (size_t len = 1; len < 8; ++len) {
    testLen(len);
  }
}

TEST(follyMemcpy, offset) {
  for (size_t dst_offset = 0; dst_offset < 32; dst_offset += 7) {
    for (size_t src_offset = 0; src_offset < 32; src_offset += 7) {
      for (size_t len = 8; len < 1000; len += 7) {
        testLen(len, dst_offset, src_offset);
      }
    }
  }
}

TEST(follyMemcpy, offsetHuge) {
  size_t kHuge = 49 * 4096;
  testLen(kHuge, 0, 0);
  testLen(kHuge, 16, 16);
  testLen(kHuge, 32, 32);
  testLen(kHuge, 7, 31);
  testLen(kHuge, 31, 2);
  testLen(kHuge, 0, 31);
  testLen(kHuge, 7, 0);
}

TEST(follyMemcpy, overlap) {
  static constexpr ssize_t kStartIndex = 1000;

  std::array<char, 2000> copy_buf;
  std::array<char, 2000> check_buf;

  for (ssize_t copy_size = 0; copy_size < 300; copy_size++) {
    for (ssize_t overlap_offset = -copy_size - 1;
         overlap_offset <= copy_size + 1;
         overlap_offset++) {
      for (size_t i = 0; i < check_buf.size(); i++) {
        copy_buf[i] = static_cast<char>(i % 128);
      }
      memset(check_buf.data(), static_cast<char>(-1), check_buf.size());

      memmove(check_buf.data(), copy_buf.data() + kStartIndex, copy_size);
      // Call __folly_memcpy directly so that asan doesn't complain about the
      // overlapping memcpy.
      folly::__folly_memcpy(
          copy_buf.data() + kStartIndex + overlap_offset,
          copy_buf.data() + kStartIndex,
          copy_size);

      for (ssize_t i = 1000 + overlap_offset - 1;
           i < 100 + overlap_offset + copy_size + 1;
           i++) {
        printf(
            "i: %zd, val: %c\n",
            i,
            *(copy_buf.data() + kStartIndex + overlap_offset + i));
      }

      for (ssize_t i = 0; i < copy_size; i++) {
        ASSERT_EQ(
            *(copy_buf.data() + kStartIndex + overlap_offset + i),
            *(check_buf.data() + i))
            << "Error after __folly_memcpy(src + " << kStartIndex << " + "
            << overlap_offset << ", src + " << kStartIndex << ", " << copy_size
            << ") at index i = " << i;
      }
    }
  }
}

TEST(follyMemcpy, main) {
  for (size_t len = 8; len <= 128; ++len) {
    testLen(len);
  }

  for (size_t len = 128; len <= kSize; len += 128) {
    testLen(len);
  }

  for (size_t len = 128; len <= kSize; len += 73) {
    testLen(len);
  }
}
