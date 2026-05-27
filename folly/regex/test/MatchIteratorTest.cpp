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

#include <string>
#include <vector>

#include <folly/portability/GTest.h>
#include <folly/regex/Regex.h>

using namespace folly::regex;

// ===== MatchIterator / MatchRange Tests =====
//
// These tests exercise the MatchIterator and MatchRange types returned by
// Regex::matchAll(), focusing on iteration semantics, zero-length match
// advancement, group offset adjustment, and edge cases.

// --- Zero-length match advancement ---

TEST(MatchIteratorTest, ZeroLengthMatchAdvancement) {
  // a* matches empty string at every position; the iterator must advance
  // pos by 1 on each zero-length match to avoid infinite loops.
  constexpr auto re = compile<"a*">();
  std::vector<std::string> matches;
  for (auto m : re.matchAll("bbb")) {
    matches.emplace_back(m[0]);
  }
  // Expect empty matches at positions 0, 1, 2, 3 (after last char).
  std::vector<std::string> expected{"", "", "", ""};
  EXPECT_EQ(matches, expected);
}

TEST(MatchIteratorTest, ZeroLengthMatchMixedWithNonEmpty) {
  // a* on "aba": matches "a" at 0, "" at 1, "a" at 2, "" at 3.
  constexpr auto re = compile<"a*">();
  std::vector<std::string> matches;
  for (auto m : re.matchAll("aba")) {
    matches.emplace_back(m[0]);
  }
  std::vector<std::string> expected{"a", "", "a", ""};
  EXPECT_EQ(matches, expected);
}

// --- Empty pattern ---

TEST(MatchIteratorTest, EmptyPatternMatchesAtEveryPosition) {
  constexpr auto re = compile<"">();
  std::vector<std::size_t> offsets;
  std::string_view input = "abc";
  for (auto m : re.matchAll(input)) {
    offsets.push_back(static_cast<std::size_t>(m[0].data() - input.data()));
  }
  // Empty pattern matches at positions 0, 1, 2, 3.
  std::vector<std::size_t> expected{0, 1, 2, 3};
  EXPECT_EQ(offsets, expected);
}

TEST(MatchIteratorTest, EmptyPatternOnEmptyInput) {
  constexpr auto re = compile<"">();
  std::vector<std::string> matches;
  for (auto m : re.matchAll("")) {
    matches.emplace_back(m[0]);
  }
  // Single empty match at position 0.
  std::vector<std::string> expected{""};
  EXPECT_EQ(matches, expected);
}

// --- Match at string boundaries ---

TEST(MatchIteratorTest, MatchAtFirstChar) {
  constexpr auto re = compile<"\\d+">();
  std::vector<std::string> matches;
  for (auto m : re.matchAll("42abc")) {
    matches.emplace_back(m[0]);
  }
  std::vector<std::string> expected{"42"};
  EXPECT_EQ(matches, expected);
}

TEST(MatchIteratorTest, MatchAtLastChar) {
  constexpr auto re = compile<"\\d+">();
  std::vector<std::string> matches;
  for (auto m : re.matchAll("abc42")) {
    matches.emplace_back(m[0]);
  }
  std::vector<std::string> expected{"42"};
  EXPECT_EQ(matches, expected);
}

TEST(MatchIteratorTest, MatchAtBothBoundaries) {
  constexpr auto re = compile<"\\d+">();
  std::vector<std::string> matches;
  for (auto m : re.matchAll("1abc2")) {
    matches.emplace_back(m[0]);
  }
  std::vector<std::string> expected{"1", "2"};
  EXPECT_EQ(matches, expected);
}

// --- Consecutive non-overlapping matches ---

TEST(MatchIteratorTest, ConsecutiveNonOverlapping) {
  constexpr auto re = compile<"ab">();
  std::vector<std::string> matches;
  for (auto m : re.matchAll("ababab")) {
    matches.emplace_back(m[0]);
  }
  std::vector<std::string> expected{"ab", "ab", "ab"};
  EXPECT_EQ(matches, expected);
}

TEST(MatchIteratorTest, NonOverlappingSkipsOverlaps) {
  // "aa" in "aaa" should match at position 0 only, not at position 1.
  constexpr auto re = compile<"aa">();
  std::vector<std::string> matches;
  std::string_view input = "aaa";
  for (auto m : re.matchAll(input)) {
    matches.emplace_back(m[0]);
  }
  EXPECT_EQ(matches.size(), 1u);
  EXPECT_EQ(matches[0], "aa");
}

TEST(MatchIteratorTest, AdjacentSingleCharMatches) {
  constexpr auto re = compile<".">();
  std::vector<std::string> matches;
  for (auto m : re.matchAll("abc")) {
    matches.emplace_back(m[0]);
  }
  std::vector<std::string> expected{"a", "b", "c"};
  EXPECT_EQ(matches, expected);
}

// --- Iterator equality ---

TEST(MatchIteratorTest, BeginEqualsEndWhenNoMatches) {
  constexpr auto re = compile<"\\d+">();
  auto range = re.matchAll("no digits here");
  EXPECT_EQ(range.begin(), range.end());
}

TEST(MatchIteratorTest, BeginNotEqualsEndWhenMatchExists) {
  constexpr auto re = compile<"\\d+">();
  auto range = re.matchAll("abc 42");
  EXPECT_NE(range.begin(), range.end());
}

TEST(MatchIteratorTest, DefaultConstructedIteratorIsEnd) {
  using Re = Regex<"\\d+">;
  Re::MatchIterator it;
  Re::MatchIterator end;
  EXPECT_EQ(it, end);
}

// --- Post-increment returns previous value ---

TEST(MatchIteratorTest, PostIncrementReturnsPreviousValue) {
  constexpr auto re = compile<"\\d+">();
  auto range = re.matchAll("12 34 56");
  auto it = range.begin();

  auto prev = it++;
  EXPECT_EQ((*prev)[0], "12");
  EXPECT_EQ((*it)[0], "34");
}

TEST(MatchIteratorTest, PostIncrementAtLastMatch) {
  constexpr auto re = compile<"\\d+">();
  auto range = re.matchAll("42");
  auto it = range.begin();

  auto prev = it++;
  EXPECT_EQ((*prev)[0], "42");
  EXPECT_EQ(it, range.end());
}

// --- Group offsets relative to original input ---

TEST(MatchIteratorTest, GroupOffsetsRelativeToOriginalInput) {
  // Captures must point into the original input string, not the
  // remaining substring passed to doSearch.
  constexpr auto re = compile<"(\\d+)">();
  std::string_view input = "abc 12 def 34";
  std::vector<std::string> fullMatches;
  std::vector<std::string> group1Matches;
  std::vector<std::size_t> group1Offsets;
  for (auto m : re.matchAll(input)) {
    fullMatches.emplace_back(m[0]);
    group1Matches.emplace_back(m[1]);
    group1Offsets.push_back(
        static_cast<std::size_t>(m[1].data() - input.data()));
  }
  std::vector<std::string> expectedFull{"12", "34"};
  std::vector<std::string> expectedGroup1{"12", "34"};
  EXPECT_EQ(fullMatches, expectedFull);
  EXPECT_EQ(group1Matches, expectedGroup1);
  // "12" starts at offset 4, "34" starts at offset 11.
  std::vector<std::size_t> expectedOffsets{4, 11};
  EXPECT_EQ(group1Offsets, expectedOffsets);
}

TEST(MatchIteratorTest, GroupOffsetsWithMultipleCaptures) {
  constexpr auto re = compile<"(\\w+)=(\\w+)">();
  std::string_view input = "foo=bar baz=qux";
  std::vector<std::string> keys, values;
  for (auto m : re.matchAll(input)) {
    keys.emplace_back(m[1]);
    values.emplace_back(m[2]);
  }
  std::vector<std::string> expectedKeys{"foo", "baz"};
  std::vector<std::string> expectedValues{"bar", "qux"};
  EXPECT_EQ(keys, expectedKeys);
  EXPECT_EQ(values, expectedValues);
}

TEST(MatchIteratorTest, GroupDataPointsIntoOriginalInput) {
  constexpr auto re = compile<"(\\d+)">();
  std::string_view input = "xx 99 yy";
  for (auto m : re.matchAll(input)) {
    // The group's data pointer must be within the original input buffer.
    EXPECT_GE(m[0].data(), input.data());
    EXPECT_LE(m[0].data() + m[0].size(), input.data() + input.size());
    EXPECT_GE(m[1].data(), input.data());
    EXPECT_LE(m[1].data() + m[1].size(), input.data() + input.size());
  }
}

// --- matchAll on empty input ---

TEST(MatchIteratorTest, MatchAllEmptyInputNoMatch) {
  constexpr auto re = compile<"\\d+">();
  std::vector<std::string> matches;
  for (auto m : re.matchAll("")) {
    matches.emplace_back(m[0]);
  }
  EXPECT_TRUE(matches.empty());
}

TEST(MatchIteratorTest, MatchAllEmptyInputWithZeroLengthPattern) {
  constexpr auto re = compile<"a?">();
  std::vector<std::string> matches;
  for (auto m : re.matchAll("")) {
    matches.emplace_back(m[0]);
  }
  // a? matches empty at position 0.
  std::vector<std::string> expected{""};
  EXPECT_EQ(matches, expected);
}

// --- Single-char matches don't skip characters ---

TEST(MatchIteratorTest, SingleCharMatchesNoSkip) {
  constexpr auto re = compile<"[a-z]">();
  std::vector<std::string> matches;
  for (auto m : re.matchAll("a1b2c")) {
    matches.emplace_back(m[0]);
  }
  std::vector<std::string> expected{"a", "b", "c"};
  EXPECT_EQ(matches, expected);
}

TEST(MatchIteratorTest, SingleCharMatchesEveryChar) {
  constexpr auto re = compile<"[abc]">();
  std::vector<std::string> matches;
  for (auto m : re.matchAll("abcabc")) {
    matches.emplace_back(m[0]);
  }
  std::vector<std::string> expected{"a", "b", "c", "a", "b", "c"};
  EXPECT_EQ(matches, expected);
}

// --- Additional edge cases ---

TEST(MatchIteratorTest, MatchAllWholeInputIsOneMatch) {
  constexpr auto re = compile<".*">();
  std::vector<std::string> matches;
  for (auto m : re.matchAll("hello")) {
    matches.emplace_back(m[0]);
  }
  // .* matches "hello" at position 0, then "" at position 5.
  std::vector<std::string> expected{"hello", ""};
  EXPECT_EQ(matches, expected);
}

TEST(MatchIteratorTest, RangeBasedForLoop) {
  constexpr auto re = compile<"\\w+">();
  int count = 0;
  for ([[maybe_unused]] auto m : re.matchAll("one two three")) {
    ++count;
  }
  EXPECT_EQ(count, 3);
}

TEST(MatchIteratorTest, DereferenceArrowOperator) {
  constexpr auto re = compile<"(\\d+)">();
  auto range = re.matchAll("42");
  auto it = range.begin();
  // operator-> returns pointer to MatchResult.
  EXPECT_EQ(it->operator[](0), "42");
  EXPECT_EQ(it->operator[](1), "42");
}

TEST(MatchIteratorTest, PreIncrementReturnsSelf) {
  constexpr auto re = compile<"\\d+">();
  auto range = re.matchAll("1 2 3");
  auto it = range.begin();
  auto& ref = ++it;
  EXPECT_EQ(&ref, &it);
  EXPECT_EQ((*it)[0], "2");
}
