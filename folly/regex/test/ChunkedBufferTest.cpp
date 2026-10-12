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

#include <folly/regex/detail/ChunkedBuffer.h>

#include <folly/portability/GTest.h>

using namespace folly::regex::detail;

// ---- Compile-time (constexpr) tests via static_assert ----

// Empty buffer has zero count.
static_assert([] {
  ChunkedBuffer<int> buf;
  return buf.total_count_ == 0;
}());

// Append a single item.
static_assert([] {
  ChunkedBuffer<int> buf;
  int val = 42;
  buf.append(&val, 1);
  return buf.total_count_ == 1 && buf[0] == 42;
}());

// Append multiple items at once.
static_assert([] {
  ChunkedBuffer<int> buf;
  int vals[] = {10, 20, 30};
  buf.append(vals, 3);
  return buf.total_count_ == 3 && buf[0] == 10 && buf[1] == 20 && buf[2] == 30;
}());

// Append zero items returns nullptr.
static_assert([] {
  ChunkedBuffer<int> buf;
  return buf.append(nullptr, 0) == nullptr;
}());

// appendOne adds a default-constructed item and returns its index.
static_assert([] {
  ChunkedBuffer<int> buf;
  int idx = buf.appendOne();
  return idx == 0 && buf.total_count_ == 1 && buf[0] == 0;
}());

// Multiple appendOne calls produce sequential indices.
static_assert([] {
  ChunkedBuffer<int> buf;
  int a = buf.appendOne();
  int b = buf.appendOne();
  int c = buf.appendOne();
  return a == 0 && b == 1 && c == 2 && buf.total_count_ == 3;
}());

// Indexed access via operator[].
static_assert([] {
  ChunkedBuffer<int> buf;
  int vals[] = {100, 200, 300, 400, 500};
  buf.append(vals, 5);
  return buf[0] == 100 && buf[2] == 300 && buf[4] == 500;
}());

// Indexed write via operator[] modifies the stored value.
static_assert([] {
  ChunkedBuffer<int> buf;
  int val = 10;
  buf.append(&val, 1);
  buf[0] = 99;
  return buf[0] == 99;
}());

// Fill exactly one block (BlockSize = 64 by default).
static_assert([] {
  ChunkedBuffer<int> buf;
  for (int i = 0; i < 64; ++i) {
    int v = i;
    buf.append(&v, 1);
  }
  return buf.total_count_ == 64 && buf[0] == 0 && buf[63] == 63;
}());

// Overflow into a second block at item 65.
static_assert([] {
  ChunkedBuffer<int> buf;
  for (int i = 0; i < 65; ++i) {
    int v = i * 10;
    buf.append(&v, 1);
  }
  return buf.total_count_ == 65 && buf[0] == 0 && buf[63] == 630 &&
      buf[64] == 640;
}());

// Fill exactly two blocks (128 items).
static_assert([] {
  ChunkedBuffer<int> buf;
  for (int i = 0; i < 128; ++i) {
    int v = i;
    buf.append(&v, 1);
  }
  return buf.total_count_ == 128 && buf[0] == 0 && buf[63] == 63 &&
      buf[64] == 64 && buf[127] == 127;
}());

// Overflow into a third block (129 items).
static_assert([] {
  ChunkedBuffer<int> buf;
  for (int i = 0; i < 129; ++i) {
    int v = i;
    buf.append(&v, 1);
  }
  return buf.total_count_ == 129 && buf[128] == 128;
}());

// linearize copies all data into a contiguous output array.
static_assert([] {
  ChunkedBuffer<int> buf;
  int vals[] = {1, 2, 3, 4, 5};
  buf.append(vals, 5);

  int out[5] = {};
  buf.linearize(out);
  return out[0] == 1 && out[1] == 2 && out[2] == 3 && out[3] == 4 &&
      out[4] == 5;
}());

// linearize across block boundaries.
static_assert([] {
  ChunkedBuffer<int> buf;
  for (int i = 0; i < 70; ++i) {
    int v = i;
    buf.append(&v, 1);
  }

  int out[70] = {};
  buf.linearize(out);
  return out[0] == 0 && out[63] == 63 && out[64] == 64 && out[69] == 69;
}());

// Bulk append that exactly fills a block boundary.
static_assert([] {
  ChunkedBuffer<int> buf;
  int first[60] = {};
  for (int i = 0; i < 60; ++i) {
    first[i] = i;
  }
  buf.append(first, 60);
  // Remaining capacity in first block: 4. Next append of 10 goes to a new
  // block.
  int second[10] = {};
  for (int i = 0; i < 10; ++i) {
    second[i] = 100 + i;
  }
  buf.append(second, 10);
  return buf.total_count_ == 70 && buf[59] == 59 && buf[60] == 100 &&
      buf[69] == 109;
}());

// Custom BlockSize template parameter.
static_assert([] {
  ChunkedBuffer<int, 4> buf;
  for (int i = 0; i < 10; ++i) {
    int v = i;
    buf.append(&v, 1);
  }
  return buf.total_count_ == 10 && buf[0] == 0 && buf[3] == 3 && buf[4] == 4 &&
      buf[9] == 9;
}());

// Custom BlockSize linearize.
static_assert([] {
  ChunkedBuffer<int, 2> buf;
  for (int i = 0; i < 7; ++i) {
    int v = i * 3;
    buf.append(&v, 1);
  }
  int out[7] = {};
  buf.linearize(out);
  return out[0] == 0 && out[1] == 3 && out[2] == 6 && out[3] == 9 &&
      out[4] == 12 && out[5] == 15 && out[6] == 18;
}());

// Const operator[] returns the correct value.
static_assert([] {
  ChunkedBuffer<int> buf;
  int vals[] = {7, 8, 9};
  buf.append(vals, 3);
  const auto& cbuf = buf;
  return cbuf[0] == 7 && cbuf[1] == 8 && cbuf[2] == 9;
}());

// append returns a pointer to the first appended item.
static_assert([] {
  ChunkedBuffer<int> buf;
  int vals[] = {1, 2, 3};
  int* ptr = buf.append(vals, 3);
  return ptr != nullptr && *ptr == 1 && *(ptr + 1) == 2 && *(ptr + 2) == 3;
}());

// Items within a single append call are contiguous in memory.
static_assert([] {
  ChunkedBuffer<int> buf;
  int vals[] = {10, 20, 30, 40};
  int* ptr = buf.append(vals, 4);
  return ptr[0] == 10 && ptr[1] == 20 && ptr[2] == 30 && ptr[3] == 40;
}());

// ---- Runtime (gtest) tests ----

TEST(ChunkedBufferTest, EmptyBuffer) {
  ChunkedBuffer<int> buf;
  EXPECT_EQ(buf.total_count_, 0);
}

TEST(ChunkedBufferTest, AppendSingleItem) {
  ChunkedBuffer<int> buf;
  int val = 42;
  buf.append(&val, 1);
  EXPECT_EQ(buf.total_count_, 1);
  EXPECT_EQ(buf[0], 42);
}

TEST(ChunkedBufferTest, AppendMultipleItems) {
  ChunkedBuffer<int> buf;
  int vals[] = {10, 20, 30, 40, 50};
  buf.append(vals, 5);
  EXPECT_EQ(buf.total_count_, 5);
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(buf[i], (i + 1) * 10) << "index " << i;
  }
}

TEST(ChunkedBufferTest, AppendZeroItems) {
  ChunkedBuffer<int> buf;
  EXPECT_EQ(buf.append(nullptr, 0), nullptr);
  EXPECT_EQ(buf.total_count_, 0);
}

TEST(ChunkedBufferTest, AppendOne) {
  ChunkedBuffer<int> buf;
  int idx0 = buf.appendOne();
  int idx1 = buf.appendOne();
  EXPECT_EQ(idx0, 0);
  EXPECT_EQ(idx1, 1);
  EXPECT_EQ(buf.total_count_, 2);
  EXPECT_EQ(buf[0], 0);
  EXPECT_EQ(buf[1], 0);
}

TEST(ChunkedBufferTest, IndexedWrite) {
  ChunkedBuffer<int> buf;
  int val = 0;
  buf.append(&val, 1);
  buf[0] = 999;
  EXPECT_EQ(buf[0], 999);
}

TEST(ChunkedBufferTest, FillOneBlock) {
  ChunkedBuffer<int> buf;
  for (int i = 0; i < 64; ++i) {
    int v = i;
    buf.append(&v, 1);
  }
  EXPECT_EQ(buf.total_count_, 64);
  EXPECT_EQ(buf[0], 0);
  EXPECT_EQ(buf[63], 63);
}

TEST(ChunkedBufferTest, OverflowToSecondBlock) {
  ChunkedBuffer<int> buf;
  for (int i = 0; i < 65; ++i) {
    int v = i;
    buf.append(&v, 1);
  }
  EXPECT_EQ(buf.total_count_, 65);
  EXPECT_EQ(buf[0], 0);
  EXPECT_EQ(buf[63], 63);
  EXPECT_EQ(buf[64], 64);
}

TEST(ChunkedBufferTest, ThreeBlocks) {
  ChunkedBuffer<int> buf;
  for (int i = 0; i < 150; ++i) {
    int v = i;
    buf.append(&v, 1);
  }
  EXPECT_EQ(buf.total_count_, 150);
  EXPECT_EQ(buf[0], 0);
  EXPECT_EQ(buf[63], 63);
  EXPECT_EQ(buf[64], 64);
  EXPECT_EQ(buf[127], 127);
  EXPECT_EQ(buf[128], 128);
  EXPECT_EQ(buf[149], 149);
}

TEST(ChunkedBufferTest, LinearizeSingleBlock) {
  ChunkedBuffer<int> buf;
  int vals[] = {5, 10, 15};
  buf.append(vals, 3);

  int out[3] = {};
  buf.linearize(out);
  EXPECT_EQ(out[0], 5);
  EXPECT_EQ(out[1], 10);
  EXPECT_EQ(out[2], 15);
}

TEST(ChunkedBufferTest, LinearizeAcrossBlocks) {
  ChunkedBuffer<int> buf;
  for (int i = 0; i < 100; ++i) {
    int v = i;
    buf.append(&v, 1);
  }

  int out[100] = {};
  buf.linearize(out);
  for (int i = 0; i < 100; ++i) {
    EXPECT_EQ(out[i], i) << "index " << i;
  }
}

TEST(ChunkedBufferTest, BulkAppendOverflow) {
  // Fill first block partially, then bulk-append more than the remaining
  // capacity.
  ChunkedBuffer<int> buf;
  int first[60] = {};
  for (int i = 0; i < 60; ++i) {
    first[i] = i;
  }
  buf.append(first, 60);
  EXPECT_EQ(buf.total_count_, 60);

  // Remaining capacity: 4. Appending 10 items forces a new block.
  int second[10] = {};
  for (int i = 0; i < 10; ++i) {
    second[i] = 100 + i;
  }
  buf.append(second, 10);
  EXPECT_EQ(buf.total_count_, 70);

  EXPECT_EQ(buf[59], 59);
  EXPECT_EQ(buf[60], 100);
  EXPECT_EQ(buf[69], 109);
}

TEST(ChunkedBufferTest, CustomBlockSize) {
  ChunkedBuffer<int, 4> buf;
  for (int i = 0; i < 12; ++i) {
    int v = i;
    buf.append(&v, 1);
  }
  EXPECT_EQ(buf.total_count_, 12);

  // Items should span 3 blocks of size 4.
  for (int i = 0; i < 12; ++i) {
    EXPECT_EQ(buf[i], i) << "index " << i;
  }

  int out[12] = {};
  buf.linearize(out);
  for (int i = 0; i < 12; ++i) {
    EXPECT_EQ(out[i], i) << "linearized index " << i;
  }
}

TEST(ChunkedBufferTest, AppendReturnsPointerToFirstItem) {
  ChunkedBuffer<int> buf;
  int vals[] = {7, 8, 9};
  int* ptr = buf.append(vals, 3);
  ASSERT_NE(ptr, nullptr);
  EXPECT_EQ(*ptr, 7);
  EXPECT_EQ(*(ptr + 1), 8);
  EXPECT_EQ(*(ptr + 2), 9);
}

TEST(ChunkedBufferTest, ContiguityWithinSingleAppend) {
  ChunkedBuffer<int> buf;
  int vals[] = {10, 20, 30, 40, 50};
  int* ptr = buf.append(vals, 5);
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(ptr[i], (i + 1) * 10);
  }
}

TEST(ChunkedBufferTest, ConstIndexedAccess) {
  ChunkedBuffer<int> buf;
  int vals[] = {1, 2, 3};
  buf.append(vals, 3);
  const auto& cbuf = buf;
  EXPECT_EQ(cbuf[0], 1);
  EXPECT_EQ(cbuf[1], 2);
  EXPECT_EQ(cbuf[2], 3);
}

TEST(ChunkedBufferTest, LargeBufferStress) {
  // Verify correctness with many items across many blocks.
  constexpr int kCount = 1000;
  ChunkedBuffer<int> buf;
  for (int i = 0; i < kCount; ++i) {
    int v = i;
    buf.append(&v, 1);
  }
  EXPECT_EQ(buf.total_count_, kCount);

  // Spot-check various positions.
  EXPECT_EQ(buf[0], 0);
  EXPECT_EQ(buf[63], 63);
  EXPECT_EQ(buf[64], 64);
  EXPECT_EQ(buf[127], 127);
  EXPECT_EQ(buf[500], 500);
  EXPECT_EQ(buf[999], 999);

  // Verify full linearize.
  int out[kCount] = {};
  buf.linearize(out);
  for (int i = 0; i < kCount; ++i) {
    EXPECT_EQ(out[i], i) << "index " << i;
  }
}

TEST(ChunkedBufferTest, StructItems) {
  struct Pair {
    int a = 0;
    int b = 0;
  };
  ChunkedBuffer<Pair, 4> buf;
  for (int i = 0; i < 10; ++i) {
    Pair p{i, i * 2};
    buf.append(&p, 1);
  }
  EXPECT_EQ(buf.total_count_, 10);
  EXPECT_EQ(buf[0].a, 0);
  EXPECT_EQ(buf[0].b, 0);
  EXPECT_EQ(buf[5].a, 5);
  EXPECT_EQ(buf[5].b, 10);
  EXPECT_EQ(buf[9].a, 9);
  EXPECT_EQ(buf[9].b, 18);
}
