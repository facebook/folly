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

#include <folly/regex/detail/FixedBitset.h>

#include <vector>

#include <folly/portability/GTest.h>

using namespace folly::regex::detail;

// ---- Compile-time (constexpr) tests via static_assert ----

// Default-constructed bitset is empty.
static_assert([] {
  FixedBitset<64> bs;
  return bs.empty();
}());

// Set and test a single bit.
static_assert([] {
  FixedBitset<64> bs;
  bs.set(0);
  return bs.test(0) && !bs.test(1);
}());

// Set bit at word boundary (bit 63).
static_assert([] {
  FixedBitset<128> bs;
  bs.set(63);
  return bs.test(63) && !bs.test(62) && !bs.test(64);
}());

// Set bit at word boundary (bit 64).
static_assert([] {
  FixedBitset<128> bs;
  bs.set(64);
  return bs.test(64) && !bs.test(63) && !bs.test(65);
}());

// Set bit at boundary 127.
static_assert([] {
  FixedBitset<128> bs;
  bs.set(127);
  return bs.test(127) && !bs.test(126);
}());

// Set and clear a bit.
static_assert([] {
  FixedBitset<64> bs;
  bs.set(5);
  bs.clear(5);
  return !bs.test(5) && bs.empty();
}());

// Multiple bits across words.
static_assert([] {
  FixedBitset<256> bs;
  bs.set(0);
  bs.set(63);
  bs.set(64);
  bs.set(127);
  bs.set(128);
  bs.set(255);
  return bs.test(0) && bs.test(63) && bs.test(64) && bs.test(127) &&
      bs.test(128) && bs.test(255) && !bs.test(1) && !bs.test(129);
}());

// empty() returns false when a bit is set.
static_assert([] {
  FixedBitset<64> bs;
  bs.set(10);
  return !bs.empty();
}());

// clearAll resets all bits.
static_assert([] {
  FixedBitset<128> bs;
  bs.set(0);
  bs.set(63);
  bs.set(64);
  bs.set(127);
  bs.clearAll();
  return bs.empty();
}());

// Equality: identical bitsets are equal.
static_assert([] {
  FixedBitset<128> a;
  FixedBitset<128> b;
  a.set(10);
  a.set(70);
  b.set(10);
  b.set(70);
  return a == b;
}());

// Equality: different bitsets are not equal.
static_assert([] {
  FixedBitset<128> a;
  FixedBitset<128> b;
  a.set(10);
  b.set(11);
  return a != b;
}());

// orWith: union of two bitsets.
static_assert([] {
  FixedBitset<128> a;
  FixedBitset<128> b;
  a.set(10);
  b.set(70);
  a.orWith(b);
  return a.test(10) && a.test(70);
}());

// andWith: intersection of two bitsets.
static_assert([] {
  FixedBitset<128> a;
  FixedBitset<128> b;
  a.set(10);
  a.set(70);
  b.set(70);
  b.set(100);
  a.andWith(b);
  return !a.test(10) && a.test(70) && !a.test(100);
}());

// andNotWith: difference of two bitsets.
static_assert([] {
  FixedBitset<128> a;
  FixedBitset<128> b;
  a.set(10);
  a.set(70);
  b.set(70);
  a.andNotWith(b);
  return a.test(10) && !a.test(70);
}());

// anyIntersection: true when bitsets overlap.
static_assert([] {
  FixedBitset<128> a;
  FixedBitset<128> b;
  a.set(70);
  b.set(70);
  return a.anyIntersection(b);
}());

// anyIntersection: false when bitsets don't overlap.
static_assert([] {
  FixedBitset<128> a;
  FixedBitset<128> b;
  a.set(10);
  b.set(70);
  return !a.anyIntersection(b);
}());

// isSubsetOf: A ⊆ B.
static_assert([] {
  FixedBitset<128> a;
  FixedBitset<128> b;
  a.set(10);
  b.set(10);
  b.set(70);
  return a.isSubsetOf(b);
}());

// isSubsetOf: A ⊄ B when A has bits not in B.
static_assert([] {
  FixedBitset<128> a;
  FixedBitset<128> b;
  a.set(10);
  a.set(70);
  b.set(10);
  return !a.isSubsetOf(b);
}());

// isSubsetOf: empty is always a subset.
static_assert([] {
  FixedBitset<128> a;
  FixedBitset<128> b;
  b.set(10);
  return a.isSubsetOf(b);
}());

// copyFrom: copy from a smaller bitset.
static_assert([] {
  FixedBitset<64> small;
  small.set(5);
  small.set(63);

  FixedBitset<128> big;
  big.set(100); // should be cleared by copyFrom
  big.copyFrom(small);
  return big.test(5) && big.test(63) && !big.test(100);
}());

// copyFrom: copy from a larger bitset (truncates).
static_assert([] {
  FixedBitset<128> big;
  big.set(5);
  big.set(100);

  FixedBitset<64> small;
  small.copyFrom(big);
  return small.test(5) && !small.test(63);
}());

// copyFrom: same-size copy.
static_assert([] {
  FixedBitset<128> a;
  a.set(0);
  a.set(64);
  a.set(127);

  FixedBitset<128> b;
  b.copyFrom(a);
  return b.test(0) && b.test(64) && b.test(127);
}());

// hash: same bitsets produce the same hash.
static_assert([] {
  FixedBitset<128> a;
  FixedBitset<128> b;
  a.set(10);
  a.set(70);
  b.set(10);
  b.set(70);
  return a.hash() == b.hash();
}());

// hash: different bitsets produce different hashes (probabilistic).
static_assert([] {
  FixedBitset<128> a;
  FixedBitset<128> b;
  a.set(10);
  b.set(11);
  return a.hash() != b.hash();
}());

// hash: empty bitset has a deterministic hash.
static_assert([] {
  FixedBitset<64> a;
  FixedBitset<64> b;
  return a.hash() == b.hash();
}());

// forEachSetBit: iterates all set bits.
static_assert([] {
  FixedBitset<128> bs;
  bs.set(0);
  bs.set(5);
  bs.set(63);
  bs.set(64);
  bs.set(127);

  int bits[5] = {};
  int count = 0;
  bs.forEachSetBit([&](int i) { bits[count++] = i; });
  return count == 5 && bits[0] == 0 && bits[1] == 5 && bits[2] == 63 &&
      bits[3] == 64 && bits[4] == 127;
}());

// forEachSetBit: empty bitset calls no callback.
static_assert([] {
  FixedBitset<64> bs;
  int count = 0;
  bs.forEachSetBit([&](int) { ++count; });
  return count == 0;
}());

// Single-bit bitset (MaxBits = 1).
static_assert([] {
  FixedBitset<1> bs;
  bs.set(0);
  return bs.test(0) && !bs.empty();
}());

// ---- Runtime (gtest) tests ----

TEST(FixedBitsetTest, DefaultEmpty) {
  FixedBitset<64> bs;
  EXPECT_TRUE(bs.empty());
  for (int i = 0; i < 64; ++i) {
    EXPECT_FALSE(bs.test(i)) << "bit " << i;
  }
}

TEST(FixedBitsetTest, SetAndTest) {
  FixedBitset<256> bs;
  bs.set(0);
  bs.set(63);
  bs.set(64);
  bs.set(127);
  bs.set(128);
  bs.set(255);

  EXPECT_TRUE(bs.test(0));
  EXPECT_TRUE(bs.test(63));
  EXPECT_TRUE(bs.test(64));
  EXPECT_TRUE(bs.test(127));
  EXPECT_TRUE(bs.test(128));
  EXPECT_TRUE(bs.test(255));

  EXPECT_FALSE(bs.test(1));
  EXPECT_FALSE(bs.test(62));
  EXPECT_FALSE(bs.test(65));
  EXPECT_FALSE(bs.test(126));
  EXPECT_FALSE(bs.test(129));
  EXPECT_FALSE(bs.test(254));
}

TEST(FixedBitsetTest, Clear) {
  FixedBitset<128> bs;
  bs.set(10);
  bs.set(70);
  EXPECT_TRUE(bs.test(10));
  EXPECT_TRUE(bs.test(70));

  bs.clear(10);
  EXPECT_FALSE(bs.test(10));
  EXPECT_TRUE(bs.test(70));

  bs.clear(70);
  EXPECT_FALSE(bs.test(70));
  EXPECT_TRUE(bs.empty());
}

TEST(FixedBitsetTest, ClearAll) {
  FixedBitset<256> bs;
  for (int i = 0; i < 256; ++i) {
    bs.set(i);
  }
  EXPECT_FALSE(bs.empty());
  bs.clearAll();
  EXPECT_TRUE(bs.empty());
  for (int i = 0; i < 256; ++i) {
    EXPECT_FALSE(bs.test(i));
  }
}

TEST(FixedBitsetTest, OrWith) {
  FixedBitset<128> a;
  FixedBitset<128> b;
  a.set(0);
  a.set(10);
  b.set(10);
  b.set(70);
  a.orWith(b);

  EXPECT_TRUE(a.test(0));
  EXPECT_TRUE(a.test(10));
  EXPECT_TRUE(a.test(70));
}

TEST(FixedBitsetTest, AndWith) {
  FixedBitset<128> a;
  FixedBitset<128> b;
  a.set(10);
  a.set(70);
  a.set(100);
  b.set(70);
  b.set(100);
  b.set(120);
  a.andWith(b);

  EXPECT_FALSE(a.test(10));
  EXPECT_TRUE(a.test(70));
  EXPECT_TRUE(a.test(100));
  EXPECT_FALSE(a.test(120));
}

TEST(FixedBitsetTest, AndNotWith) {
  FixedBitset<128> a;
  FixedBitset<128> b;
  a.set(10);
  a.set(70);
  a.set(100);
  b.set(70);
  b.set(120);
  a.andNotWith(b);

  EXPECT_TRUE(a.test(10));
  EXPECT_FALSE(a.test(70));
  EXPECT_TRUE(a.test(100));
  EXPECT_FALSE(a.test(120));
}

TEST(FixedBitsetTest, AnyIntersection) {
  FixedBitset<128> a;
  FixedBitset<128> b;

  // No overlap.
  a.set(10);
  b.set(70);
  EXPECT_FALSE(a.anyIntersection(b));

  // Add overlap.
  b.set(10);
  EXPECT_TRUE(a.anyIntersection(b));

  // Both empty.
  FixedBitset<128> c;
  FixedBitset<128> d;
  EXPECT_FALSE(c.anyIntersection(d));
}

TEST(FixedBitsetTest, IsSubsetOf) {
  FixedBitset<128> a;
  FixedBitset<128> b;

  // Empty is subset of anything.
  EXPECT_TRUE(a.isSubsetOf(b));

  b.set(10);
  b.set(70);
  EXPECT_TRUE(a.isSubsetOf(b));

  a.set(10);
  EXPECT_TRUE(a.isSubsetOf(b));

  a.set(100);
  EXPECT_FALSE(a.isSubsetOf(b));

  // Identical sets.
  FixedBitset<128> c;
  c.set(10);
  c.set(70);
  EXPECT_TRUE(c.isSubsetOf(b));
  EXPECT_TRUE(b.isSubsetOf(c));
}

TEST(FixedBitsetTest, Equality) {
  FixedBitset<128> a;
  FixedBitset<128> b;
  EXPECT_EQ(a, b);

  a.set(10);
  EXPECT_NE(a, b);

  b.set(10);
  EXPECT_EQ(a, b);

  a.set(127);
  EXPECT_NE(a, b);
}

TEST(FixedBitsetTest, ForEachSetBit) {
  FixedBitset<256> bs;
  bs.set(0);
  bs.set(5);
  bs.set(63);
  bs.set(64);
  bs.set(127);
  bs.set(128);
  bs.set(255);

  std::vector<int> bits;
  bs.forEachSetBit([&](int i) { bits.push_back(i); });

  std::vector<int> expected = {0, 5, 63, 64, 127, 128, 255};
  EXPECT_EQ(bits, expected);
}

TEST(FixedBitsetTest, ForEachSetBitEmpty) {
  FixedBitset<128> bs;
  int count = 0;
  bs.forEachSetBit([&](int) { ++count; });
  EXPECT_EQ(count, 0);
}

TEST(FixedBitsetTest, ForEachSetBitAllSet) {
  FixedBitset<64> bs;
  for (int i = 0; i < 64; ++i) {
    bs.set(i);
  }
  std::vector<int> bits;
  bs.forEachSetBit([&](int i) { bits.push_back(i); });
  EXPECT_EQ(bits.size(), 64u);
  for (int i = 0; i < 64; ++i) {
    EXPECT_EQ(bits[i], i);
  }
}

TEST(FixedBitsetTest, Hash) {
  FixedBitset<128> a;
  FixedBitset<128> b;
  a.set(10);
  a.set(70);
  b.set(10);
  b.set(70);
  EXPECT_EQ(a.hash(), b.hash());

  FixedBitset<128> c;
  c.set(11);
  EXPECT_NE(a.hash(), c.hash());
}

TEST(FixedBitsetTest, HashEmptyDeterministic) {
  FixedBitset<64> a;
  FixedBitset<64> b;
  EXPECT_EQ(a.hash(), b.hash());
}

TEST(FixedBitsetTest, HashDifferentSingleBits) {
  // Different single bits should produce different hashes.
  FixedBitset<256> a;
  FixedBitset<256> b;
  a.set(0);
  b.set(1);
  EXPECT_NE(a.hash(), b.hash());

  FixedBitset<256> c;
  c.set(128);
  EXPECT_NE(a.hash(), c.hash());
  EXPECT_NE(b.hash(), c.hash());
}

TEST(FixedBitsetTest, CopyFromSmallerToLarger) {
  FixedBitset<64> small;
  small.set(0);
  small.set(63);

  FixedBitset<256> big;
  big.set(200);
  big.copyFrom(small);

  EXPECT_TRUE(big.test(0));
  EXPECT_TRUE(big.test(63));
  EXPECT_FALSE(big.test(200)); // Excess bits should be cleared.
}

TEST(FixedBitsetTest, CopyFromLargerToSmaller) {
  FixedBitset<256> big;
  big.set(0);
  big.set(63);
  big.set(200);

  FixedBitset<64> small;
  small.copyFrom(big);

  EXPECT_TRUE(small.test(0));
  EXPECT_TRUE(small.test(63));
  // Bit 200 is outside the small bitset's range.
}

TEST(FixedBitsetTest, CopyFromSameSize) {
  FixedBitset<128> a;
  a.set(0);
  a.set(64);
  a.set(127);

  FixedBitset<128> b;
  b.copyFrom(a);
  EXPECT_EQ(a, b);
}

TEST(FixedBitsetTest, CopyFromClearsExcess) {
  FixedBitset<128> target;
  target.set(100);
  target.set(120);

  FixedBitset<64> source;
  source.set(5);

  target.copyFrom(source);
  EXPECT_TRUE(target.test(5));
  EXPECT_FALSE(target.test(100));
  EXPECT_FALSE(target.test(120));
}

TEST(FixedBitsetTest, WordBoundaryBits) {
  // Test every word boundary bit for correctness.
  FixedBitset<256> bs;

  for (int bit :
       {0,
        1,
        62,
        63,
        64,
        65,
        126,
        127,
        128,
        129,
        190,
        191,
        192,
        193,
        254,
        255}) {
    bs.clearAll();
    bs.set(bit);
    EXPECT_TRUE(bs.test(bit)) << "bit " << bit;
    // Adjacent bits should not be set.
    if (bit > 0) {
      EXPECT_FALSE(bs.test(bit - 1)) << "bit " << bit << " - 1";
    }
    if (bit < 255) {
      EXPECT_FALSE(bs.test(bit + 1)) << "bit " << bit << " + 1";
    }
  }
}

TEST(FixedBitsetTest, OperationsAcrossWordBoundaries) {
  // Test orWith, andWith, andNotWith across word boundaries.
  FixedBitset<128> a;
  FixedBitset<128> b;

  // Set bits in both words for both bitsets.
  a.set(30);
  a.set(90);
  b.set(30);
  b.set(100);

  // orWith: should have {30, 90, 100}.
  FixedBitset<128> orResult;
  orResult.copyFrom(a);
  orResult.orWith(b);
  EXPECT_TRUE(orResult.test(30));
  EXPECT_TRUE(orResult.test(90));
  EXPECT_TRUE(orResult.test(100));

  // andWith: should have {30}.
  FixedBitset<128> andResult;
  andResult.copyFrom(a);
  andResult.andWith(b);
  EXPECT_TRUE(andResult.test(30));
  EXPECT_FALSE(andResult.test(90));
  EXPECT_FALSE(andResult.test(100));

  // andNotWith: a & ~b = {90}.
  FixedBitset<128> andNotResult;
  andNotResult.copyFrom(a);
  andNotResult.andNotWith(b);
  EXPECT_FALSE(andNotResult.test(30));
  EXPECT_TRUE(andNotResult.test(90));
  EXPECT_FALSE(andNotResult.test(100));
}

TEST(FixedBitsetTest, SingleBitBitset) {
  FixedBitset<1> bs;
  EXPECT_TRUE(bs.empty());
  bs.set(0);
  EXPECT_TRUE(bs.test(0));
  EXPECT_FALSE(bs.empty());
  bs.clear(0);
  EXPECT_TRUE(bs.empty());
}

TEST(FixedBitsetTest, AllBitsSet) {
  FixedBitset<128> bs;
  for (int i = 0; i < 128; ++i) {
    bs.set(i);
  }
  for (int i = 0; i < 128; ++i) {
    EXPECT_TRUE(bs.test(i)) << "bit " << i;
  }
  EXPECT_FALSE(bs.empty());
}
