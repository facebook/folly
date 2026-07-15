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

#include <folly/regex/detail/CharClass.h>

#include <folly/portability/GTest.h>

using namespace folly::regex::detail;

// ===================================================================
// charClassTest — flat range array membership
// ===================================================================

TEST(CharClassTest, SingleRangeMatch) {
  CharRange ranges[] = {{'a', 'z'}};
  EXPECT_TRUE(charClassTest(ranges, 1, 'a'));
  EXPECT_TRUE(charClassTest(ranges, 1, 'm'));
  EXPECT_TRUE(charClassTest(ranges, 1, 'z'));
  EXPECT_FALSE(charClassTest(ranges, 1, 'A'));
  EXPECT_FALSE(charClassTest(ranges, 1, '0'));
}

TEST(CharClassTest, MultipleRanges) {
  CharRange ranges[] = {{'0', '9'}, {'A', 'Z'}, {'a', 'z'}};
  EXPECT_TRUE(charClassTest(ranges, 3, '0'));
  EXPECT_TRUE(charClassTest(ranges, 3, '9'));
  EXPECT_TRUE(charClassTest(ranges, 3, 'A'));
  EXPECT_TRUE(charClassTest(ranges, 3, 'Z'));
  EXPECT_TRUE(charClassTest(ranges, 3, 'a'));
  EXPECT_TRUE(charClassTest(ranges, 3, 'z'));
  EXPECT_FALSE(charClassTest(ranges, 3, '!'));
  EXPECT_FALSE(charClassTest(ranges, 3, ' '));
  EXPECT_FALSE(charClassTest(ranges, 3, '{'));
}

TEST(CharClassTest, SingleCharRange) {
  CharRange ranges[] = {{'x', 'x'}};
  EXPECT_TRUE(charClassTest(ranges, 1, 'x'));
  EXPECT_FALSE(charClassTest(ranges, 1, 'w'));
  EXPECT_FALSE(charClassTest(ranges, 1, 'y'));
}

TEST(CharClassTest, FullByteRange) {
  CharRange ranges[] = {{0, 255}};
  for (int c = 0; c < 256; ++c) {
    EXPECT_TRUE(charClassTest(ranges, 1, static_cast<char>(c)));
  }
}

TEST(CharClassTest, EmptyRangeArray) {
  EXPECT_FALSE(charClassTest(nullptr, 0, 'a'));
}

TEST(CharClassTest, GapBetweenRanges) {
  // Ranges with a gap: [a-f] [m-z]
  CharRange ranges[] = {{'a', 'f'}, {'m', 'z'}};
  EXPECT_TRUE(charClassTest(ranges, 2, 'c'));
  EXPECT_TRUE(charClassTest(ranges, 2, 'p'));
  EXPECT_FALSE(charClassTest(ranges, 2, 'g'));
  EXPECT_FALSE(charClassTest(ranges, 2, 'l'));
}

// ===================================================================
// setRangeBits — bitmap population
// ===================================================================

TEST(CharClassTest, SetRangeBitsSingleBit) {
  uint64_t words[4] = {};
  setRangeBits(words, 0, 5, 5);
  EXPECT_TRUE((words[0] >> 5) & 1);
  // Adjacent bits should not be set.
  EXPECT_FALSE((words[0] >> 4) & 1);
  EXPECT_FALSE((words[0] >> 6) & 1);
}

TEST(CharClassTest, SetRangeBitsWithinOneWord) {
  uint64_t words[4] = {};
  setRangeBits(words, 0, 10, 20);
  for (int i = 0; i < 64; ++i) {
    bool expected = (i >= 10 && i <= 20);
    EXPECT_EQ(static_cast<bool>((words[0] >> i) & 1), expected) << "bit " << i;
  }
}

TEST(CharClassTest, SetRangeBitsCrossWord) {
  uint64_t words[4] = {};
  setRangeBits(words, 0, 60, 70);
  // Bits 60-63 in word 0.
  for (int i = 60; i <= 63; ++i) {
    EXPECT_TRUE((words[0] >> i) & 1) << "bit " << i;
  }
  // Bits 64-70 in word 1 (shifted by 64).
  for (int i = 0; i <= 6; ++i) {
    EXPECT_TRUE((words[1] >> i) & 1) << "bit " << (64 + i);
  }
  EXPECT_FALSE((words[1] >> 7) & 1);
}

TEST(CharClassTest, SetRangeBitsWithOffset) {
  uint64_t words[4] = {};
  unsigned char lo = 48; // '0'
  setRangeBits(words, lo, 48, 57); // '0'-'9'
  // Bits 0-9 in the bitmap.
  for (int i = 0; i < 10; ++i) {
    EXPECT_TRUE((words[0] >> i) & 1) << "bit " << i;
  }
  EXPECT_FALSE((words[0] >> 10) & 1);
}

TEST(CharClassTest, SetRangeBitsFullWord) {
  uint64_t words[1] = {};
  setRangeBits(words, 0, 0, 63);
  EXPECT_EQ(words[0], ~uint64_t(0));
}

TEST(CharClassTest, SetRangeBitsMultipleFullWords) {
  uint64_t words[4] = {};
  setRangeBits(words, 0, 0, 191);
  EXPECT_EQ(words[0], ~uint64_t(0));
  EXPECT_EQ(words[1], ~uint64_t(0));
  EXPECT_EQ(words[2], ~uint64_t(0));
  EXPECT_EQ(words[3], 0u);
}

// ===================================================================
// compactBitmapTest — O(1) bitmap lookup
// ===================================================================

TEST(CharClassTest, CompactBitmapBasic) {
  uint64_t words[2] = {};
  unsigned char lo = 'a';
  setRangeBits(words, lo, 'a', 'z');

  for (char c = 'a'; c <= 'z'; ++c) {
    EXPECT_TRUE(compactBitmapTest(words, lo, 2, c)) << "char '" << c << "'";
  }
  EXPECT_FALSE(compactBitmapTest(words, lo, 2, 'A'));
  EXPECT_FALSE(compactBitmapTest(words, lo, 2, '0'));
  EXPECT_FALSE(compactBitmapTest(words, lo, 2, '{'));
}

TEST(CharClassTest, CompactBitmapBelowLo) {
  uint64_t words[1] = {};
  setRangeBits(words, 100, 100, 110);
  EXPECT_FALSE(compactBitmapTest(words, 100, 1, static_cast<char>(99)));
}

TEST(CharClassTest, CompactBitmapAboveRange) {
  uint64_t words[1] = {};
  setRangeBits(words, 0, 0, 10);
  // word_count=1 covers bits 0-63. Char 64 is out of range.
  EXPECT_FALSE(compactBitmapTest(words, 0, 1, static_cast<char>(64)));
}

TEST(CharClassTest, SubsetOfIdentical) {
  CharRange a[] = {{'a', 'z'}};
  CharRange b[] = {{'a', 'z'}};
  EXPECT_TRUE(charClassIsSubsetOf(a, 1, b, 1));
}

TEST(CharClassTest, SubsetOfLarger) {
  CharRange a[] = {{'d', 'm'}};
  CharRange b[] = {{'a', 'z'}};
  EXPECT_TRUE(charClassIsSubsetOf(a, 1, b, 1));
}

TEST(CharClassTest, NotSubsetOfSmaller) {
  CharRange a[] = {{'a', 'z'}};
  CharRange b[] = {{'d', 'm'}};
  EXPECT_FALSE(charClassIsSubsetOf(a, 1, b, 1));
}

TEST(CharClassTest, SubsetOfMultipleRanges) {
  CharRange a[] = {{'0', '9'}};
  CharRange b[] = {{'0', '9'}, {'a', 'z'}};
  EXPECT_TRUE(charClassIsSubsetOf(a, 1, b, 2));
}

TEST(CharClassTest, NotSubsetPartialOverlap) {
  CharRange a[] = {{'a', 'm'}};
  CharRange b[] = {{'f', 'z'}};
  EXPECT_FALSE(charClassIsSubsetOf(a, 1, b, 1));
}

// ===================================================================
// CharRangeSet — builder with linked blocks
// ===================================================================

TEST(CharClassTest, CharRangeSetAddSingleChar) {
  CharRangeSet rs;
  rs.addChar('x');
  EXPECT_EQ(rs.count(), 1);
  EXPECT_TRUE(rs.test('x'));
  EXPECT_FALSE(rs.test('y'));
}

TEST(CharClassTest, CharRangeSetAddRange) {
  CharRangeSet rs;
  rs.addRange('a', 'z');
  EXPECT_EQ(rs.count(), 1); // One merged range.
  for (char c = 'a'; c <= 'z'; ++c) {
    EXPECT_TRUE(rs.test(c)) << "char '" << c << "'";
  }
  EXPECT_FALSE(rs.test('A'));
  EXPECT_FALSE(rs.test('0'));
}

TEST(CharClassTest, CharRangeSetMergeOverlapping) {
  CharRangeSet rs;
  rs.addRange('a', 'm');
  rs.addRange('j', 'z');
  EXPECT_EQ(rs.count(), 1); // Overlapping ranges merge.
  EXPECT_TRUE(rs.test('a'));
  EXPECT_TRUE(rs.test('m'));
  EXPECT_TRUE(rs.test('n'));
  EXPECT_TRUE(rs.test('z'));
}

TEST(CharClassTest, CharRangeSetMergeAdjacent) {
  CharRangeSet rs;
  rs.addRange('a', 'f');
  rs.addRange('g', 'z');
  EXPECT_EQ(rs.count(), 1); // Adjacent ranges merge (g = f+1).
  EXPECT_TRUE(rs.test('a'));
  EXPECT_TRUE(rs.test('f'));
  EXPECT_TRUE(rs.test('g'));
  EXPECT_TRUE(rs.test('z'));
}

TEST(CharClassTest, CharRangeSetNonOverlapping) {
  CharRangeSet rs;
  rs.addRange('a', 'f');
  rs.addRange('m', 'z');
  EXPECT_EQ(rs.count(), 2); // Gap prevents merge.
  EXPECT_TRUE(rs.test('a'));
  EXPECT_TRUE(rs.test('f'));
  EXPECT_FALSE(rs.test('g'));
  EXPECT_TRUE(rs.test('m'));
  EXPECT_TRUE(rs.test('z'));
}

TEST(CharClassTest, CharRangeSetInvertSimple) {
  CharRangeSet rs;
  rs.addRange('a', 'z');
  rs.invert();
  // After inversion: [\x00-\x60] and [\x7b-\xff]
  EXPECT_FALSE(rs.test('a'));
  EXPECT_FALSE(rs.test('z'));
  EXPECT_TRUE(rs.test('A'));
  EXPECT_TRUE(rs.test('0'));
  EXPECT_TRUE(rs.test('\0'));
  EXPECT_TRUE(rs.test('\xff'));
}

TEST(CharClassTest, CharRangeSetInvertFullRange) {
  CharRangeSet rs;
  rs.addRange(0, 255);
  rs.invert();
  EXPECT_EQ(rs.count(), 0);
  for (int c = 0; c < 256; ++c) {
    EXPECT_FALSE(rs.test(static_cast<char>(c)));
  }
}

TEST(CharClassTest, CharRangeSetInvertEmpty) {
  CharRangeSet rs;
  rs.invert();
  // Inverting empty → full range.
  EXPECT_EQ(rs.count(), 1);
  for (int c = 0; c < 256; ++c) {
    EXPECT_TRUE(rs.test(static_cast<char>(c)));
  }
}

TEST(CharClassTest, CharRangeSetInvertMultipleGaps) {
  CharRangeSet rs;
  rs.addRange('a', 'f'); // [97-102]
  rs.addRange('m', 'r'); // [109-114]
  rs.invert();
  // Should produce: [0-96], [103-108], [115-255]
  EXPECT_TRUE(rs.test('\0'));
  EXPECT_TRUE(rs.test('`')); // 96
  EXPECT_FALSE(rs.test('a'));
  EXPECT_FALSE(rs.test('f'));
  EXPECT_TRUE(rs.test('g')); // 103
  EXPECT_TRUE(rs.test('l')); // 108
  EXPECT_FALSE(rs.test('m'));
  EXPECT_FALSE(rs.test('r'));
  EXPECT_TRUE(rs.test('s')); // 115
  EXPECT_TRUE(rs.test('\xff'));
}

TEST(CharClassTest, CharRangeSetMerge) {
  CharRangeSet a;
  a.addRange('a', 'f');
  CharRangeSet b;
  b.addRange('d', 'z');
  a.merge(b);
  EXPECT_EQ(a.count(), 1);
  EXPECT_TRUE(a.test('a'));
  EXPECT_TRUE(a.test('z'));
}

TEST(CharClassTest, CharRangeSetEquality) {
  CharRangeSet a;
  a.addRange('a', 'z');
  CharRangeSet b;
  b.addRange('a', 'z');
  EXPECT_TRUE(a == b);

  CharRangeSet c;
  c.addRange('a', 'y');
  EXPECT_FALSE(a == c);
}

TEST(CharClassTest, CharRangeSetForEach) {
  CharRangeSet rs;
  rs.addRange('0', '9');
  rs.addRange('A', 'F');

  int rangeCount = 0;
  CharRange seen[2] = {};
  rs.forEach([&](CharRange r) {
    if (rangeCount < 2) {
      seen[rangeCount] = r;
    }
    ++rangeCount;
  });
  EXPECT_EQ(rangeCount, 2);
  EXPECT_EQ(seen[0].lo, '0');
  EXPECT_EQ(seen[0].hi, '9');
  EXPECT_EQ(seen[1].lo, 'A');
  EXPECT_EQ(seen[1].hi, 'F');
}

TEST(CharClassTest, CharRangeSetManyCharsOverflowBlock) {
  // CharRangeBlock::kCapacity = 4. Adding more than 4 non-overlapping
  // ranges forces block chain allocation.
  CharRangeSet rs;
  rs.addRange(0, 10);
  rs.addRange(20, 30);
  rs.addRange(40, 50);
  rs.addRange(60, 70);
  rs.addRange(80, 90);
  rs.addRange(100, 110);
  EXPECT_EQ(rs.count(), 6);
  EXPECT_TRUE(rs.test(5));
  EXPECT_TRUE(rs.test(25));
  EXPECT_TRUE(rs.test(85));
  EXPECT_TRUE(rs.test(105));
  EXPECT_FALSE(rs.test(11));
  EXPECT_FALSE(rs.test(75));
}

TEST(CharClassTest, CharRangeSetAddCharMergesIntoRange) {
  CharRangeSet rs;
  rs.addRange('a', 'e');
  rs.addChar('f');
  EXPECT_EQ(rs.count(), 1); // 'f' is adjacent to 'e', merges.
  EXPECT_TRUE(rs.test('f'));
}

TEST(CharClassTest, CharRangeSetMoveConstructor) {
  CharRangeSet a;
  a.addRange('a', 'z');
  a.addRange('0', '9');
  int origCount = a.count();

  CharRangeSet b(std::move(a));
  EXPECT_EQ(b.count(), origCount);
  EXPECT_TRUE(b.test('m'));
  EXPECT_TRUE(b.test('5'));
}

// ===================================================================
// FirstCharFilter — lightweight char set for search acceleration
// ===================================================================

TEST(CharClassTest, FirstCharFilterSingleChar) {
  FirstCharFilter f;
  f.accepts_all = false;
  f.addChar('x');
  EXPECT_TRUE(f.test('x'));
  EXPECT_FALSE(f.test('y'));
  EXPECT_FALSE(f.accepts_all);
}

TEST(CharClassTest, FirstCharFilterSingleRange) {
  FirstCharFilter f;
  f.accepts_all = false;
  f.addRange('a', 'z');
  EXPECT_TRUE(f.test('a'));
  EXPECT_TRUE(f.test('z'));
  EXPECT_FALSE(f.test('A'));
}

TEST(CharClassTest, FirstCharFilterAcceptsAllBypass) {
  FirstCharFilter f;
  f.accepts_all = true;
  EXPECT_TRUE(f.test('a'));
  EXPECT_TRUE(f.test('\0'));
  EXPECT_TRUE(f.test('\xff'));
}

TEST(CharClassTest, FirstCharFilterMergeOverlapping) {
  FirstCharFilter f;
  f.accepts_all = false;
  f.addRange('a', 'm');
  f.addRange('j', 'z');
  EXPECT_EQ(f.range_count, 1);
  EXPECT_TRUE(f.test('a'));
  EXPECT_TRUE(f.test('z'));
}

TEST(CharClassTest, FirstCharFilterMergeAdjacent) {
  FirstCharFilter f;
  f.accepts_all = false;
  f.addRange('a', 'f');
  f.addRange('g', 'z');
  EXPECT_EQ(f.range_count, 1);
}

TEST(CharClassTest, FirstCharFilterMergeFrom) {
  FirstCharFilter f;
  f.accepts_all = false;
  CharRange ranges[] = {{'0', '9'}, {'a', 'f'}};
  f.mergeFrom(ranges, 2);
  EXPECT_EQ(f.range_count, 2);
  EXPECT_TRUE(f.test('5'));
  EXPECT_TRUE(f.test('c'));
  EXPECT_FALSE(f.test('g'));
}

TEST(CharClassTest, FirstCharFilterOverflowToAcceptsAll) {
  FirstCharFilter f;
  f.accepts_all = false;
  // kMaxRanges = 8. Add 9 non-adjacent ranges to force overflow.
  for (int i = 0; i < 9; ++i) {
    unsigned char lo = static_cast<unsigned char>(i * 20);
    unsigned char hi = static_cast<unsigned char>(i * 20 + 5);
    f.addRange(lo, hi);
    if (f.accepts_all) {
      break;
    }
  }
  EXPECT_TRUE(f.accepts_all);
}

TEST(CharClassTest, FirstCharFilterMergeFromOverflow) {
  FirstCharFilter f;
  f.accepts_all = false;
  // Fill up to kMaxRanges.
  for (int i = 0; i < FirstCharFilter::kMaxRanges; ++i) {
    unsigned char lo = static_cast<unsigned char>(i * 20);
    unsigned char hi = static_cast<unsigned char>(i * 20 + 5);
    f.addRange(lo, hi);
  }
  EXPECT_FALSE(f.accepts_all);

  // One more non-adjacent range causes overflow.
  CharRange extra[] = {{250, 255}};
  f.mergeFrom(extra, 1);
  EXPECT_TRUE(f.accepts_all);
}

// ===================================================================
// areRangeSetsDisjoint — O(n+m) merge walk
// ===================================================================

TEST(CharClassTest, DisjointNonOverlapping) {
  CharRange a[] = {{'a', 'f'}};
  CharRange b[] = {{'m', 'z'}};
  EXPECT_TRUE(areRangeSetsDisjoint(a, 1, b, 1));
}

TEST(CharClassTest, DisjointOverlapping) {
  CharRange a[] = {{'a', 'm'}};
  CharRange b[] = {{'f', 'z'}};
  EXPECT_FALSE(areRangeSetsDisjoint(a, 1, b, 1));
}

TEST(CharClassTest, DisjointSameRange) {
  CharRange a[] = {{'a', 'z'}};
  CharRange b[] = {{'a', 'z'}};
  EXPECT_FALSE(areRangeSetsDisjoint(a, 1, b, 1));
}

TEST(CharClassTest, DisjointAdjacent) {
  // [a-f] and [g-z] are adjacent but disjoint (no overlap).
  CharRange a[] = {{'a', 'f'}};
  CharRange b[] = {{'g', 'z'}};
  EXPECT_TRUE(areRangeSetsDisjoint(a, 1, b, 1));
}

TEST(CharClassTest, DisjointEmptyA) {
  CharRange b[] = {{'a', 'z'}};
  EXPECT_TRUE(areRangeSetsDisjoint(nullptr, 0, b, 1));
}

TEST(CharClassTest, DisjointEmptyBoth) {
  EXPECT_TRUE(areRangeSetsDisjoint(nullptr, 0, nullptr, 0));
}

TEST(CharClassTest, DisjointMultipleRanges) {
  CharRange a[] = {{'a', 'f'}, {'m', 'r'}};
  CharRange b[] = {{'g', 'l'}, {'s', 'z'}};
  EXPECT_TRUE(areRangeSetsDisjoint(a, 2, b, 2));
}

TEST(CharClassTest, DisjointMultipleRangesOverlap) {
  CharRange a[] = {{'a', 'f'}, {'m', 'r'}};
  CharRange b[] = {{'e', 'l'}, {'s', 'z'}};
  EXPECT_FALSE(areRangeSetsDisjoint(a, 2, b, 2));
}

TEST(CharClassTest, DisjointSinglePointOverlap) {
  CharRange a[] = {{'a', 'f'}};
  CharRange b[] = {{'f', 'z'}};
  EXPECT_FALSE(areRangeSetsDisjoint(a, 1, b, 1));
}

// ===================================================================
// charClassIsSubsetOf — subset check
// ===================================================================

TEST(CharClassTest, EmptyIsSubsetOfAnything) {
  CharRange b[] = {{'a', 'z'}};
  EXPECT_TRUE(charClassIsSubsetOf(nullptr, 0, b, 1));
}

TEST(CharClassTest, EmptyIsSubsetOfEmpty) {
  EXPECT_TRUE(charClassIsSubsetOf(nullptr, 0, nullptr, 0));
}

// ===================================================================
// encodeUtf8 — codepoint to UTF-8 byte sequence
// ===================================================================

TEST(CharClassTest, EncodeUtf8Ascii) {
  char buf[4] = {};
  EXPECT_EQ(encodeUtf8(0x41, buf), 1); // 'A'
  EXPECT_EQ(buf[0], 'A');
}

TEST(CharClassTest, EncodeUtf8Null) {
  char buf[4] = {};
  EXPECT_EQ(encodeUtf8(0, buf), 1);
  EXPECT_EQ(buf[0], '\0');
}

TEST(CharClassTest, EncodeUtf8MaxAscii) {
  char buf[4] = {};
  EXPECT_EQ(encodeUtf8(0x7F, buf), 1);
  EXPECT_EQ(buf[0], '\x7F');
}

TEST(CharClassTest, EncodeUtf8TwoByte) {
  char buf[4] = {};
  EXPECT_EQ(encodeUtf8(0x80, buf), 2);
  EXPECT_EQ(static_cast<unsigned char>(buf[0]), 0xC2);
  EXPECT_EQ(static_cast<unsigned char>(buf[1]), 0x80);
}

TEST(CharClassTest, EncodeUtf8TwoByteEAcute) {
  // U+00E9 (é)
  char buf[4] = {};
  EXPECT_EQ(encodeUtf8(0xE9, buf), 2);
  EXPECT_EQ(static_cast<unsigned char>(buf[0]), 0xC3);
  EXPECT_EQ(static_cast<unsigned char>(buf[1]), 0xA9);
}

TEST(CharClassTest, EncodeUtf8MaxTwoByte) {
  char buf[4] = {};
  EXPECT_EQ(encodeUtf8(0x7FF, buf), 2);
  EXPECT_EQ(static_cast<unsigned char>(buf[0]), 0xDF);
  EXPECT_EQ(static_cast<unsigned char>(buf[1]), 0xBF);
}

TEST(CharClassTest, EncodeUtf8ThreeByte) {
  char buf[4] = {};
  EXPECT_EQ(encodeUtf8(0x800, buf), 3);
  EXPECT_EQ(static_cast<unsigned char>(buf[0]), 0xE0);
  EXPECT_EQ(static_cast<unsigned char>(buf[1]), 0xA0);
  EXPECT_EQ(static_cast<unsigned char>(buf[2]), 0x80);
}

TEST(CharClassTest, EncodeUtf8ThreeByteChinese) {
  // U+4F60 (你)
  char buf[4] = {};
  EXPECT_EQ(encodeUtf8(0x4F60, buf), 3);
  EXPECT_EQ(static_cast<unsigned char>(buf[0]), 0xE4);
  EXPECT_EQ(static_cast<unsigned char>(buf[1]), 0xBD);
  EXPECT_EQ(static_cast<unsigned char>(buf[2]), 0xA0);
}

TEST(CharClassTest, EncodeUtf8MaxThreeByte) {
  char buf[4] = {};
  EXPECT_EQ(encodeUtf8(0xFFFF, buf), 3);
  EXPECT_EQ(static_cast<unsigned char>(buf[0]), 0xEF);
  EXPECT_EQ(static_cast<unsigned char>(buf[1]), 0xBF);
  EXPECT_EQ(static_cast<unsigned char>(buf[2]), 0xBF);
}

TEST(CharClassTest, EncodeUtf8FourByte) {
  char buf[4] = {};
  EXPECT_EQ(encodeUtf8(0x10000, buf), 4);
  EXPECT_EQ(static_cast<unsigned char>(buf[0]), 0xF0);
  EXPECT_EQ(static_cast<unsigned char>(buf[1]), 0x90);
  EXPECT_EQ(static_cast<unsigned char>(buf[2]), 0x80);
  EXPECT_EQ(static_cast<unsigned char>(buf[3]), 0x80);
}

TEST(CharClassTest, EncodeUtf8FourByteEmoji) {
  // U+1F600 (😀)
  char buf[4] = {};
  EXPECT_EQ(encodeUtf8(0x1F600, buf), 4);
  EXPECT_EQ(static_cast<unsigned char>(buf[0]), 0xF0);
  EXPECT_EQ(static_cast<unsigned char>(buf[1]), 0x9F);
  EXPECT_EQ(static_cast<unsigned char>(buf[2]), 0x98);
  EXPECT_EQ(static_cast<unsigned char>(buf[3]), 0x80);
}

TEST(CharClassTest, EncodeUtf8MaxCodepoint) {
  char buf[4] = {};
  EXPECT_EQ(encodeUtf8(0x10FFFF, buf), 4);
  EXPECT_EQ(static_cast<unsigned char>(buf[0]), 0xF4);
  EXPECT_EQ(static_cast<unsigned char>(buf[1]), 0x8F);
  EXPECT_EQ(static_cast<unsigned char>(buf[2]), 0xBF);
  EXPECT_EQ(static_cast<unsigned char>(buf[3]), 0xBF);
}

// ===================================================================
// Character classification helpers
// ===================================================================

TEST(CharClassTest, IsDigit) {
  for (char c = '0'; c <= '9'; ++c) {
    EXPECT_TRUE(isDigit(c));
  }
  EXPECT_FALSE(isDigit('a'));
  EXPECT_FALSE(isDigit('/'));
  EXPECT_FALSE(isDigit(':'));
}

TEST(CharClassTest, IsAlpha) {
  for (char c = 'a'; c <= 'z'; ++c) {
    EXPECT_TRUE(isAlpha(c));
  }
  for (char c = 'A'; c <= 'Z'; ++c) {
    EXPECT_TRUE(isAlpha(c));
  }
  EXPECT_FALSE(isAlpha('0'));
  EXPECT_FALSE(isAlpha('_'));
  EXPECT_FALSE(isAlpha(' '));
}

TEST(CharClassTest, IsWordChar) {
  EXPECT_TRUE(isWordChar('a'));
  EXPECT_TRUE(isWordChar('Z'));
  EXPECT_TRUE(isWordChar('0'));
  EXPECT_TRUE(isWordChar('_'));
  EXPECT_FALSE(isWordChar(' '));
  EXPECT_FALSE(isWordChar('-'));
  EXPECT_FALSE(isWordChar('!'));
}

TEST(CharClassTest, IsSpace) {
  EXPECT_TRUE(isSpace(' '));
  EXPECT_TRUE(isSpace('\t'));
  EXPECT_TRUE(isSpace('\n'));
  EXPECT_TRUE(isSpace('\r'));
  EXPECT_TRUE(isSpace('\f'));
  EXPECT_TRUE(isSpace('\v'));
  EXPECT_FALSE(isSpace('a'));
  EXPECT_FALSE(isSpace('0'));
}

TEST(CharClassTest, AsciiToLower) {
  EXPECT_EQ(asciiToLower('A'), 'a');
  EXPECT_EQ(asciiToLower('Z'), 'z');
  EXPECT_EQ(asciiToLower('a'), 'a');
  EXPECT_EQ(asciiToLower('z'), 'z');
  EXPECT_EQ(asciiToLower('0'), '0');
  EXPECT_EQ(asciiToLower('!'), '!');
}

// ===================================================================
// Factory functions: makeDigitRanges, makeWordRanges, etc.
// ===================================================================

TEST(CharClassTest, MakeDigitRanges) {
  auto rs = makeDigitRanges();
  for (char c = '0'; c <= '9'; ++c) {
    EXPECT_TRUE(rs.test(c));
  }
  EXPECT_FALSE(rs.test('a'));
  EXPECT_FALSE(rs.test('/'));
  EXPECT_FALSE(rs.test(':'));
}

TEST(CharClassTest, MakeWordRanges) {
  auto rs = makeWordRanges();
  EXPECT_TRUE(rs.test('a'));
  EXPECT_TRUE(rs.test('z'));
  EXPECT_TRUE(rs.test('A'));
  EXPECT_TRUE(rs.test('Z'));
  EXPECT_TRUE(rs.test('0'));
  EXPECT_TRUE(rs.test('9'));
  EXPECT_TRUE(rs.test('_'));
  EXPECT_FALSE(rs.test(' '));
  EXPECT_FALSE(rs.test('-'));
  EXPECT_FALSE(rs.test('!'));
}

TEST(CharClassTest, MakeSpaceRanges) {
  auto rs = makeSpaceRanges();
  EXPECT_TRUE(rs.test(' '));
  EXPECT_TRUE(rs.test('\t'));
  EXPECT_TRUE(rs.test('\n'));
  EXPECT_TRUE(rs.test('\r'));
  EXPECT_TRUE(rs.test('\f'));
  EXPECT_TRUE(rs.test('\v'));
  EXPECT_FALSE(rs.test('a'));
  EXPECT_FALSE(rs.test('0'));
}

TEST(CharClassTest, MakeDotRanges) {
  auto rs = makeDotRanges();
  // Matches everything except '\n' (byte 10).
  for (int c = 0; c < 256; ++c) {
    bool expected = (c != '\n');
    EXPECT_EQ(rs.test(static_cast<char>(c)), expected) << "byte " << c;
  }
}

// ===================================================================
// addPosixClass — POSIX character class addition
// ===================================================================

TEST(CharClassTest, PosixClassAlpha) {
  CharRangeSet rs;
  EXPECT_TRUE(addPosixClass(rs, "alpha"));
  EXPECT_TRUE(rs.test('a'));
  EXPECT_TRUE(rs.test('z'));
  EXPECT_TRUE(rs.test('A'));
  EXPECT_TRUE(rs.test('Z'));
  EXPECT_FALSE(rs.test('0'));
  EXPECT_FALSE(rs.test('!'));
}

TEST(CharClassTest, PosixClassDigit) {
  CharRangeSet rs;
  EXPECT_TRUE(addPosixClass(rs, "digit"));
  for (char c = '0'; c <= '9'; ++c) {
    EXPECT_TRUE(rs.test(c));
  }
  EXPECT_FALSE(rs.test('a'));
}

TEST(CharClassTest, PosixClassAlnum) {
  CharRangeSet rs;
  EXPECT_TRUE(addPosixClass(rs, "alnum"));
  EXPECT_TRUE(rs.test('a'));
  EXPECT_TRUE(rs.test('Z'));
  EXPECT_TRUE(rs.test('5'));
  EXPECT_FALSE(rs.test('!'));
}

TEST(CharClassTest, PosixClassSpace) {
  CharRangeSet rs;
  EXPECT_TRUE(addPosixClass(rs, "space"));
  EXPECT_TRUE(rs.test(' '));
  EXPECT_TRUE(rs.test('\t'));
  EXPECT_TRUE(rs.test('\n'));
  EXPECT_FALSE(rs.test('a'));
}

TEST(CharClassTest, PosixClassUpper) {
  CharRangeSet rs;
  EXPECT_TRUE(addPosixClass(rs, "upper"));
  EXPECT_TRUE(rs.test('A'));
  EXPECT_TRUE(rs.test('Z'));
  EXPECT_FALSE(rs.test('a'));
  EXPECT_FALSE(rs.test('z'));
}

TEST(CharClassTest, PosixClassLower) {
  CharRangeSet rs;
  EXPECT_TRUE(addPosixClass(rs, "lower"));
  EXPECT_TRUE(rs.test('a'));
  EXPECT_TRUE(rs.test('z'));
  EXPECT_FALSE(rs.test('A'));
}

TEST(CharClassTest, PosixClassPunct) {
  CharRangeSet rs;
  EXPECT_TRUE(addPosixClass(rs, "punct"));
  EXPECT_TRUE(rs.test('!'));
  EXPECT_TRUE(rs.test('.'));
  EXPECT_TRUE(rs.test('{'));
  EXPECT_FALSE(rs.test('a'));
  EXPECT_FALSE(rs.test('0'));
  EXPECT_FALSE(rs.test(' '));
}

TEST(CharClassTest, PosixClassXdigit) {
  CharRangeSet rs;
  EXPECT_TRUE(addPosixClass(rs, "xdigit"));
  EXPECT_TRUE(rs.test('0'));
  EXPECT_TRUE(rs.test('9'));
  EXPECT_TRUE(rs.test('a'));
  EXPECT_TRUE(rs.test('f'));
  EXPECT_TRUE(rs.test('A'));
  EXPECT_TRUE(rs.test('F'));
  EXPECT_FALSE(rs.test('g'));
  EXPECT_FALSE(rs.test('G'));
}

TEST(CharClassTest, PosixClassBlank) {
  CharRangeSet rs;
  EXPECT_TRUE(addPosixClass(rs, "blank"));
  EXPECT_TRUE(rs.test('\t'));
  EXPECT_TRUE(rs.test(' '));
  EXPECT_FALSE(rs.test('\n'));
  EXPECT_FALSE(rs.test('a'));
}

TEST(CharClassTest, PosixClassAscii) {
  CharRangeSet rs;
  EXPECT_TRUE(addPosixClass(rs, "ascii"));
  EXPECT_TRUE(rs.test('\0'));
  EXPECT_TRUE(rs.test('\x7F'));
  EXPECT_FALSE(rs.test('\x80'));
}

TEST(CharClassTest, PosixClassCntrl) {
  CharRangeSet rs;
  EXPECT_TRUE(addPosixClass(rs, "cntrl"));
  EXPECT_TRUE(rs.test('\0'));
  EXPECT_TRUE(rs.test('\x1F'));
  EXPECT_TRUE(rs.test('\x7F'));
  EXPECT_FALSE(rs.test(' '));
  EXPECT_FALSE(rs.test('a'));
}

TEST(CharClassTest, PosixClassGraph) {
  CharRangeSet rs;
  EXPECT_TRUE(addPosixClass(rs, "graph"));
  EXPECT_TRUE(rs.test('!'));
  EXPECT_TRUE(rs.test('~'));
  EXPECT_FALSE(rs.test(' '));
  EXPECT_FALSE(rs.test('\0'));
}

TEST(CharClassTest, PosixClassPrint) {
  CharRangeSet rs;
  EXPECT_TRUE(addPosixClass(rs, "print"));
  EXPECT_TRUE(rs.test(' '));
  EXPECT_TRUE(rs.test('~'));
  EXPECT_FALSE(rs.test('\0'));
  EXPECT_FALSE(rs.test('\x7F'));
}

TEST(CharClassTest, PosixClassWord) {
  CharRangeSet rs;
  EXPECT_TRUE(addPosixClass(rs, "word"));
  EXPECT_TRUE(rs.test('a'));
  EXPECT_TRUE(rs.test('Z'));
  EXPECT_TRUE(rs.test('0'));
  EXPECT_TRUE(rs.test('_'));
  EXPECT_FALSE(rs.test(' '));
  EXPECT_FALSE(rs.test('!'));
}

TEST(CharClassTest, PosixClassUnknown) {
  CharRangeSet rs;
  EXPECT_FALSE(addPosixClass(rs, "unknown"));
  EXPECT_FALSE(addPosixClass(rs, ""));
  EXPECT_FALSE(addPosixClass(rs, "ALPHA"));
}

TEST(CharClassTest, PosixClassCaseInsensitiveUpper) {
  CharRangeSet rs;
  EXPECT_TRUE(addPosixClass(rs, "upper", /*caseInsensitive=*/true));
  EXPECT_TRUE(rs.test('A'));
  EXPECT_TRUE(rs.test('Z'));
  // With case-insensitive, should also include lowercase.
  EXPECT_TRUE(rs.test('a'));
  EXPECT_TRUE(rs.test('z'));
}

TEST(CharClassTest, PosixClassCaseInsensitiveLower) {
  CharRangeSet rs;
  EXPECT_TRUE(addPosixClass(rs, "lower", /*caseInsensitive=*/true));
  EXPECT_TRUE(rs.test('a'));
  EXPECT_TRUE(rs.test('z'));
  EXPECT_TRUE(rs.test('A'));
  EXPECT_TRUE(rs.test('Z'));
}

TEST(CharClassTest, PosixClassCaseInsensitiveDigitUnchanged) {
  CharRangeSet rs;
  EXPECT_TRUE(addPosixClass(rs, "digit", /*caseInsensitive=*/true));
  EXPECT_TRUE(rs.test('5'));
  EXPECT_FALSE(rs.test('a'));
}

// ===================================================================
// CharRange equality
// ===================================================================

TEST(CharClassTest, CharRangeEquality) {
  CharRange a = {'a', 'z'};
  CharRange b = {'a', 'z'};
  CharRange c = {'a', 'y'};
  CharRange d = {'b', 'z'};
  EXPECT_TRUE(a == b);
  EXPECT_FALSE(a == c);
  EXPECT_FALSE(a == d);
}
