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

#include <folly/regex/detail/DynamicBitset.h>

#include <folly/portability/GTest.h>

using namespace folly::regex::detail;

// ---- Compile-time (constexpr) tests via static_assert ----

static_assert([] {
  DynamicBitset bs;
  return bs.size() == 0;
}());

static_assert([] {
  DynamicBitset bs;
  bs.append(true);
  return bs.size() == 1 && bs.test(0);
}());

static_assert([] {
  DynamicBitset bs;
  bs.append(false);
  return bs.size() == 1 && !bs.test(0);
}());

static_assert([] {
  DynamicBitset bs;
  bs.append(true);
  bs.append(false);
  bs.append(true);
  return bs.size() == 3 && bs.test(0) && !bs.test(1) && bs.test(2);
}());

// Set and clear on appended bits.
static_assert([] {
  DynamicBitset bs;
  for (int i = 0; i < 8; ++i) {
    bs.append(false);
  }
  bs.set(3);
  bs.set(7);
  if (!bs.test(3) || !bs.test(7) || bs.test(0)) {
    return false;
  }
  bs.clear(3);
  return !bs.test(3) && bs.test(7);
}());

// Exactly 64 bits — fills one word and flushes to storage.
static_assert([] {
  DynamicBitset bs;
  for (int i = 0; i < 64; ++i) {
    bs.append(i % 2 == 0);
  }
  if (bs.size() != 64) {
    return false;
  }
  for (int i = 0; i < 64; ++i) {
    if (bs.test(i) != (i % 2 == 0)) {
      return false;
    }
  }
  return true;
}());

// 65 bits — crosses word boundary into partial word.
static_assert([] {
  DynamicBitset bs;
  for (int i = 0; i < 65; ++i) {
    bs.append(i == 64);
  }
  if (bs.size() != 65) {
    return false;
  }
  for (int i = 0; i < 64; ++i) {
    if (bs.test(i)) {
      return false;
    }
  }
  return bs.test(64);
}());

// 128 bits — exactly two complete words.
static_assert([] {
  DynamicBitset bs;
  for (int i = 0; i < 128; ++i) {
    bs.append(i == 0 || i == 63 || i == 64 || i == 127);
  }
  return bs.test(0) && bs.test(63) && bs.test(64) && bs.test(127) &&
      !bs.test(1) && !bs.test(62) && !bs.test(65) && !bs.test(126);
}());

// copyTo FixedBitset — partial word case.
static_assert([] {
  DynamicBitset bs;
  bs.append(true);
  bs.append(false);
  bs.append(true);

  FixedBitset<64> fixed;
  bs.copyTo(fixed);
  return fixed.test(0) && !fixed.test(1) && fixed.test(2) && !fixed.test(3);
}());

// copyTo FixedBitset — exactly aligned at word boundary.
static_assert([] {
  DynamicBitset bs;
  for (int i = 0; i < 64; ++i) {
    bs.append(i < 4);
  }
  FixedBitset<64> fixed;
  bs.copyTo(fixed);
  return fixed.test(0) && fixed.test(3) && !fixed.test(4) && !fixed.test(63);
}());

// copyTo FixedBitset — multi-word with partial.
static_assert([] {
  DynamicBitset bs;
  for (int i = 0; i < 100; ++i) {
    bs.append(i == 0 || i == 63 || i == 64 || i == 99);
  }
  FixedBitset<128> fixed;
  bs.copyTo(fixed);
  return fixed.test(0) && fixed.test(63) && fixed.test(64) && fixed.test(99) &&
      !fixed.test(1) && !fixed.test(98);
}());

// copyTo clears excess bits in the target.
static_assert([] {
  FixedBitset<256> fixed;
  fixed.set(200);

  DynamicBitset bs;
  bs.append(true);
  bs.copyTo(fixed);

  return fixed.test(0) && !fixed.test(200);
}());

// Set a bit in a completed (flushed) word.
static_assert([] {
  DynamicBitset bs;
  for (int i = 0; i < 128; ++i) {
    bs.append(false);
  }
  bs.set(5);
  bs.set(70);
  return bs.test(5) && bs.test(70) && !bs.test(0) && !bs.test(127);
}());

// Clear a bit in a completed (flushed) word.
static_assert([] {
  DynamicBitset bs;
  for (int i = 0; i < 128; ++i) {
    bs.append(true);
  }
  bs.clear(5);
  bs.clear(70);
  return !bs.test(5) && !bs.test(70) && bs.test(0) && bs.test(127);
}());

// Overflow into a second ChunkedBuffer block (default BlockSize=64 words
// = 4096 bits). Use 4097 bits to force one extra block allocation.
static_assert([] {
  DynamicBitset bs;
  for (int i = 0; i < 4097; ++i) {
    bs.append(i == 0 || i == 4095 || i == 4096);
  }
  if (bs.size() != 4097) {
    return false;
  }
  if (!bs.test(0) || !bs.test(4095) || !bs.test(4096)) {
    return false;
  }
  if (bs.test(1) || bs.test(4094)) {
    return false;
  }
  return true;
}());

// copyTo after ChunkedBuffer block overflow.
static_assert([] {
  DynamicBitset bs;
  for (int i = 0; i < 4097; ++i) {
    bs.append(i == 4096);
  }
  FixedBitset<8192> fixed;
  bs.copyTo(fixed);
  return fixed.test(4096) && !fixed.test(0) && !fixed.test(4095);
}());

// ---- Runtime (gtest) tests ----

TEST(DynamicBitsetTest, EmptyBitset) {
  DynamicBitset bs;
  EXPECT_EQ(bs.size(), 0);
}

TEST(DynamicBitsetTest, AppendAndTest) {
  DynamicBitset bs;
  bs.append(true);
  bs.append(false);
  bs.append(true);
  EXPECT_EQ(bs.size(), 3);
  EXPECT_TRUE(bs.test(0));
  EXPECT_FALSE(bs.test(1));
  EXPECT_TRUE(bs.test(2));
}

TEST(DynamicBitsetTest, SetClearTest) {
  DynamicBitset bs;
  for (int i = 0; i < 16; ++i) {
    bs.append(false);
  }
  bs.set(5);
  bs.set(15);
  EXPECT_TRUE(bs.test(5));
  EXPECT_TRUE(bs.test(15));
  EXPECT_FALSE(bs.test(0));

  bs.clear(5);
  EXPECT_FALSE(bs.test(5));
  EXPECT_TRUE(bs.test(15));
}

TEST(DynamicBitsetTest, WordBoundary64Bits) {
  DynamicBitset bs;
  for (int i = 0; i < 64; ++i) {
    bs.append(i % 3 == 0);
  }
  EXPECT_EQ(bs.size(), 64);
  for (int i = 0; i < 64; ++i) {
    EXPECT_EQ(bs.test(i), i % 3 == 0) << "bit " << i;
  }
}

TEST(DynamicBitsetTest, CrossWordBoundary) {
  DynamicBitset bs;
  for (int i = 0; i < 200; ++i) {
    bs.append(i == 0 || i == 63 || i == 64 || i == 127 || i == 128 || i == 199);
  }
  EXPECT_TRUE(bs.test(0));
  EXPECT_TRUE(bs.test(63));
  EXPECT_TRUE(bs.test(64));
  EXPECT_TRUE(bs.test(127));
  EXPECT_TRUE(bs.test(128));
  EXPECT_TRUE(bs.test(199));
  EXPECT_FALSE(bs.test(1));
  EXPECT_FALSE(bs.test(100));
}

TEST(DynamicBitsetTest, CopyToFixedBitset) {
  DynamicBitset bs;
  for (int i = 0; i < 100; ++i) {
    bs.append(i == 0 || i == 50 || i == 99);
  }
  FixedBitset<128> fixed;
  bs.copyTo(fixed);
  EXPECT_TRUE(fixed.test(0));
  EXPECT_TRUE(fixed.test(50));
  EXPECT_TRUE(fixed.test(99));
  EXPECT_FALSE(fixed.test(1));
  EXPECT_FALSE(fixed.test(100));
}

TEST(DynamicBitsetTest, CopyToClearsExcess) {
  FixedBitset<256> fixed;
  fixed.set(200);
  EXPECT_TRUE(fixed.test(200));

  DynamicBitset bs;
  bs.append(true);
  bs.copyTo(fixed);

  EXPECT_TRUE(fixed.test(0));
  EXPECT_FALSE(fixed.test(200));
}

TEST(DynamicBitsetTest, SetInFlushedWord) {
  DynamicBitset bs;
  for (int i = 0; i < 256; ++i) {
    bs.append(false);
  }
  // Set bits in different completed words.
  bs.set(10);
  bs.set(70);
  bs.set(200);
  EXPECT_TRUE(bs.test(10));
  EXPECT_TRUE(bs.test(70));
  EXPECT_TRUE(bs.test(200));
  EXPECT_FALSE(bs.test(0));
  EXPECT_FALSE(bs.test(255));

  // Verify via copyTo round-trip.
  FixedBitset<256> fixed;
  bs.copyTo(fixed);
  EXPECT_TRUE(fixed.test(10));
  EXPECT_TRUE(fixed.test(70));
  EXPECT_TRUE(fixed.test(200));
  EXPECT_FALSE(fixed.test(0));
}

TEST(DynamicBitsetTest, LargeMultiBlock) {
  // Exceed one ChunkedBuffer block (64 words = 4096 bits).
  DynamicBitset bs;
  constexpr int kBits = 5000;
  for (int i = 0; i < kBits; ++i) {
    bs.append(i == 0 || i == 4095 || i == 4096 || i == 4999);
  }
  EXPECT_EQ(bs.size(), kBits);
  EXPECT_TRUE(bs.test(0));
  EXPECT_TRUE(bs.test(4095));
  EXPECT_TRUE(bs.test(4096));
  EXPECT_TRUE(bs.test(4999));
  EXPECT_FALSE(bs.test(1));
  EXPECT_FALSE(bs.test(2048));

  FixedBitset<8192> fixed;
  bs.copyTo(fixed);
  EXPECT_TRUE(fixed.test(0));
  EXPECT_TRUE(fixed.test(4096));
  EXPECT_TRUE(fixed.test(4999));
  EXPECT_FALSE(fixed.test(1));
}

TEST(DynamicBitsetTest, AllBitsSet) {
  DynamicBitset bs;
  for (int i = 0; i < 130; ++i) {
    bs.append(true);
  }
  for (int i = 0; i < 130; ++i) {
    EXPECT_TRUE(bs.test(i)) << "bit " << i;
  }

  FixedBitset<192> fixed;
  bs.copyTo(fixed);
  for (int i = 0; i < 130; ++i) {
    EXPECT_TRUE(fixed.test(i)) << "bit " << i;
  }
  // Excess bits should be clear.
  EXPECT_FALSE(fixed.test(130));
  EXPECT_FALSE(fixed.test(191));
}
