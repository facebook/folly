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

#include <folly/portability/GTest.h>
#include <folly/regex/detail/Direction.h>

using namespace folly::regex::detail;

// =============================================================================
// Forward InputView — basic construction
// =============================================================================

TEST(InputViewForward, FullInput) {
  InputView<Direction::Forward> iv{"abcdef"};
  EXPECT_EQ(iv.size(), 6);
  EXPECT_EQ(iv[0], 'a');
  EXPECT_EQ(iv[5], 'f');
  EXPECT_EQ(iv.original_input, "abcdef");
}

TEST(InputViewForward, PrefixStrip) {
  // Forward prefix strips from the LEFT.
  InputView<Direction::Forward> iv{"abcdef", "ab"};
  EXPECT_EQ(iv.size(), 4);
  EXPECT_EQ(iv[0], 'c');
  EXPECT_EQ(iv[3], 'f');
  EXPECT_EQ(iv.original_input, "abcdef");
}

TEST(InputViewForward, EmptyPrefix) {
  InputView<Direction::Forward> iv{"abcdef", ""};
  EXPECT_EQ(iv.size(), 6);
  EXPECT_EQ(iv[0], 'a');
}

TEST(InputViewForward, FullPrefixStrip) {
  InputView<Direction::Forward> iv{"abc", "abc"};
  EXPECT_EQ(iv.size(), 0);
}

// =============================================================================
// Reverse InputView — basic construction
// =============================================================================

TEST(InputViewReverse, FullInput) {
  InputView<Direction::Reverse> iv{"abcdef"};
  EXPECT_EQ(iv.size(), 6);
  EXPECT_EQ(iv[0], 'a');
  EXPECT_EQ(iv[5], 'f');
}

TEST(InputViewReverse, PrefixStrip) {
  // Reverse prefix strips from the RIGHT.
  InputView<Direction::Reverse> iv{"abcdef", "ef"};
  EXPECT_EQ(iv.size(), 4);
  EXPECT_EQ(iv[0], 'a');
  EXPECT_EQ(iv[3], 'd');
}

TEST(InputViewReverse, EmptyPrefix) {
  InputView<Direction::Reverse> iv{"abcdef", ""};
  EXPECT_EQ(iv.size(), 6);
  EXPECT_EQ(iv[5], 'f');
}

// =============================================================================
// posInFull — position translation
// =============================================================================

TEST(InputViewForward, PosInFullNoStrip) {
  InputView<Direction::Forward> iv{"abcdef"};
  EXPECT_EQ(iv.posInFull(0), 0);
  EXPECT_EQ(iv.posInFull(3), 3);
  EXPECT_EQ(iv.posInFull(6), 6);
}

TEST(InputViewForward, PosInFullWithStrip) {
  InputView<Direction::Forward> iv{"abcdef", "ab"};
  // active region starts at index 2 in original_input
  EXPECT_EQ(iv.posInFull(0), 2);
  EXPECT_EQ(iv.posInFull(1), 3);
  EXPECT_EQ(iv.posInFull(3), 5);
}

TEST(InputViewReverse, PosInFullWithStrip) {
  InputView<Direction::Reverse> iv{"abcdef", "ef"};
  // reverse strips from right; active_begin is 0
  EXPECT_EQ(iv.posInFull(0), 0);
  EXPECT_EQ(iv.posInFull(3), 3);
}

// =============================================================================
// startPos, advance, canConsume, atEnd, charAt
// =============================================================================

TEST(InputViewForward, Traversal) {
  InputView<Direction::Forward> iv{"abcdef", "ab"};
  // active = "cdef", size = 4
  EXPECT_EQ(iv.startPos(), 0);
  EXPECT_TRUE(iv.canConsume(0));
  EXPECT_FALSE(iv.atEnd(0));
  EXPECT_EQ(iv.charAt(0), 'c'); // charAt(0) in Forward = iv[0]

  EXPECT_EQ(InputView<Direction::Forward>::advance(0), 1);
  EXPECT_EQ(InputView<Direction::Forward>::advance(3), 4);

  EXPECT_TRUE(iv.canConsume(3));
  EXPECT_FALSE(iv.canConsume(4));
  EXPECT_TRUE(iv.atEnd(4));
}

TEST(InputViewReverse, Traversal) {
  InputView<Direction::Reverse> iv{"abcdef", "ef"};
  // active = "abcd", size = 4
  EXPECT_EQ(iv.startPos(), 4); // reverse starts at size()
  EXPECT_TRUE(iv.canConsume(4));
  EXPECT_FALSE(iv.atEnd(4));
  EXPECT_EQ(iv.charAt(4), 'd'); // charAt(pos) in Reverse = iv[pos-1]

  EXPECT_EQ(InputView<Direction::Reverse>::advance(4), 3);
  EXPECT_EQ(InputView<Direction::Reverse>::advance(1), 0);

  EXPECT_TRUE(iv.canConsume(1));
  EXPECT_FALSE(iv.canConsume(0));
  EXPECT_TRUE(iv.atEnd(0));
}

// =============================================================================
// scanStart / scanDone
// =============================================================================

TEST(InputViewForward, ScanRange) {
  InputView<Direction::Forward> iv{"abc"};
  EXPECT_EQ(iv.scanStart(), 0);
  EXPECT_FALSE(iv.scanDone(0));
  EXPECT_FALSE(iv.scanDone(3));
  EXPECT_TRUE(iv.scanDone(4)); // past size(), wraps for size_t
}

TEST(InputViewReverse, ScanRange) {
  InputView<Direction::Reverse> iv{"abc"};
  EXPECT_EQ(iv.scanStart(), 3);
  EXPECT_FALSE(iv.scanDone(3));
  EXPECT_FALSE(iv.scanDone(0));
  // After advance(0) wraps to SIZE_MAX, which is > size()
  EXPECT_TRUE(iv.scanDone(InputView<Direction::Reverse>::advance(0)));
}

// =============================================================================
// flip — direction switching with active region reset
// =============================================================================

TEST(InputViewForward, FlipToReverseNoPrefix) {
  InputView<Direction::Forward> iv{"abcdef", "ab"};
  // Forward active = [2, 6) = "cdef"
  auto rev = iv.flip();
  // Flip removes forward left-strip, applies no reverse prefix
  // Result: active = [0, 6) = entire input
  EXPECT_EQ(rev.size(), 6);
  EXPECT_EQ(rev[0], 'a');
  EXPECT_EQ(rev[5], 'f');
  EXPECT_EQ(rev.original_input, "abcdef");
}

TEST(InputViewForward, FlipToReverseWithPrefix) {
  InputView<Direction::Forward> iv{"abcdef", "ab"};
  auto rev = iv.flip("ef");
  // Flip removes forward left-strip, applies reverse right-strip "ef"
  // Result: active = [0, 4) = "abcd"
  EXPECT_EQ(rev.size(), 4);
  EXPECT_EQ(rev[0], 'a');
  EXPECT_EQ(rev[3], 'd');
}

TEST(InputViewReverse, FlipToForwardNoPrefix) {
  InputView<Direction::Reverse> iv{"abcdef", "ef"};
  // Reverse active = [0, 4) = "abcd"
  auto fwd = iv.flip();
  // Flip removes reverse right-strip, applies no forward prefix
  // Result: active = [0, 6) = entire input
  EXPECT_EQ(fwd.size(), 6);
  EXPECT_EQ(fwd[0], 'a');
  EXPECT_EQ(fwd[5], 'f');
}

TEST(InputViewReverse, FlipToForwardWithPrefix) {
  InputView<Direction::Reverse> iv{"abcdef", "ef"};
  auto fwd = iv.flip("ab");
  // Flip removes reverse right-strip, applies forward left-strip "ab"
  // Result: active = [2, 6) = "cdef"
  EXPECT_EQ(fwd.size(), 4);
  EXPECT_EQ(fwd[0], 'c');
  EXPECT_EQ(fwd[3], 'f');
}

// =============================================================================
// fullInput — unstripped view for probes
// =============================================================================

TEST(InputViewForward, FullInputForward) {
  InputView<Direction::Forward> iv{"abcdef", "ab"};
  auto full = iv.fullInput<Direction::Forward>();
  // Full view covers entire original_input
  EXPECT_EQ(full.size(), 6);
  EXPECT_EQ(full[0], 'a');
  EXPECT_EQ(full[5], 'f');
  EXPECT_EQ(full.posInFull(0), 0);
}

TEST(InputViewForward, FullInputReverse) {
  InputView<Direction::Forward> iv{"abcdef", "ab"};
  auto full = iv.fullInput<Direction::Reverse>();
  EXPECT_EQ(full.size(), 6);
  EXPECT_EQ(full[0], 'a');
  EXPECT_EQ(full.startPos(), 6); // reverse starts at size()
}

TEST(InputViewReverse, FullInputForward) {
  InputView<Direction::Reverse> iv{"abcdef", "ef"};
  auto full = iv.fullInput<Direction::Forward>();
  EXPECT_EQ(full.size(), 6);
  EXPECT_EQ(full[0], 'a');
  EXPECT_EQ(full[5], 'f');
}

// =============================================================================
// narrowTo — active region narrowing
// =============================================================================

TEST(InputViewForward, NarrowTo) {
  InputView<Direction::Forward> iv{"abcdefgh"};
  auto narrow = iv.narrowTo(2, 6);
  // active = [2, 6) in original_input = "cdef"
  EXPECT_EQ(narrow.size(), 4);
  EXPECT_EQ(narrow[0], 'c');
  EXPECT_EQ(narrow[3], 'f');
  EXPECT_EQ(narrow.original_input, "abcdefgh");
}

TEST(InputViewForward, NarrowToWithExistingStrip) {
  InputView<Direction::Forward> iv{"abcdefgh", "ab"};
  // active = [2, 8) = "cdefgh"
  auto narrow = iv.narrowTo(1, 4);
  // narrow active = [2+1, 2+4) = [3, 6) = "def"
  EXPECT_EQ(narrow.size(), 3);
  EXPECT_EQ(narrow[0], 'd');
  EXPECT_EQ(narrow[2], 'f');
}

TEST(InputViewForward, NarrowToPreservesOriginalInput) {
  InputView<Direction::Forward> iv{"abcdefgh", "ab"};
  auto narrow = iv.narrowTo(1, 4);
  // fullInput on narrowed view still returns entire original
  auto full = narrow.fullInput<Direction::Forward>();
  EXPECT_EQ(full.size(), 8);
  EXPECT_EQ(full[0], 'a');
  EXPECT_EQ(full[7], 'h');
}

TEST(InputViewForward, NarrowToPosInFull) {
  InputView<Direction::Forward> iv{"abcdefgh", "ab"};
  // active = [2, 8)
  auto narrow = iv.narrowTo(1, 4);
  // narrow active = [3, 6)
  EXPECT_EQ(narrow.posInFull(0), 3); // 'd' in original
  EXPECT_EQ(narrow.posInFull(2), 5); // 'f' in original
}

TEST(InputViewReverse, NarrowTo) {
  InputView<Direction::Reverse> iv{"abcdefgh"};
  auto narrow = iv.narrowTo(2, 6);
  EXPECT_EQ(narrow.size(), 4);
  EXPECT_EQ(narrow[0], 'c');
  EXPECT_EQ(narrow[3], 'f');
}

// =============================================================================
// Flip preserves original_input through narrowing
// =============================================================================

TEST(InputViewForward, FlipNarrowedViewResetsRegion) {
  InputView<Direction::Forward> iv{"abcdefgh"};
  auto narrow = iv.narrowTo(2, 6); // active = "cdef"
  auto rev = narrow.flip();
  // Flip resets: removes forward left-strip, no reverse prefix
  // Result: active = [0, 8) = entire original
  EXPECT_EQ(rev.size(), 8);
  EXPECT_EQ(rev[0], 'a');
  EXPECT_EQ(rev[7], 'h');
}

// =============================================================================
// Edge cases
// =============================================================================

TEST(InputViewForward, EmptyInput) {
  InputView<Direction::Forward> iv{""};
  EXPECT_EQ(iv.size(), 0);
  EXPECT_TRUE(iv.atEnd(0));
  EXPECT_FALSE(iv.canConsume(0));
}

TEST(InputViewReverse, EmptyInput) {
  InputView<Direction::Reverse> iv{""};
  EXPECT_EQ(iv.size(), 0);
  EXPECT_EQ(iv.startPos(), 0);
  EXPECT_TRUE(iv.atEnd(0));
  EXPECT_FALSE(iv.canConsume(0));
}

TEST(InputViewForward, SingleChar) {
  InputView<Direction::Forward> iv{"x"};
  EXPECT_EQ(iv.size(), 1);
  EXPECT_EQ(iv[0], 'x');
  EXPECT_EQ(iv.charAt(0), 'x');
  EXPECT_TRUE(iv.canConsume(0));
  EXPECT_TRUE(iv.atEnd(1));
}

TEST(InputViewReverse, SingleChar) {
  InputView<Direction::Reverse> iv{"x"};
  EXPECT_EQ(iv.size(), 1);
  EXPECT_EQ(iv[0], 'x');
  EXPECT_EQ(iv.startPos(), 1);
  EXPECT_EQ(iv.charAt(1), 'x');
  EXPECT_TRUE(iv.canConsume(1));
  EXPECT_TRUE(iv.atEnd(0));
}

TEST(InputViewForward, NarrowToEmpty) {
  InputView<Direction::Forward> iv{"abcdef"};
  auto narrow = iv.narrowTo(3, 3);
  EXPECT_EQ(narrow.size(), 0);
  EXPECT_TRUE(narrow.atEnd(0));
}

TEST(InputViewForward, NarrowToSingleChar) {
  InputView<Direction::Forward> iv{"abcdef"};
  auto narrow = iv.narrowTo(2, 3);
  EXPECT_EQ(narrow.size(), 1);
  EXPECT_EQ(narrow[0], 'c');
}
