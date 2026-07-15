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

// ===== API Tests =====
//
// These tests exercise the public API surface (structured bindings,
// user-defined literals, freestanding functions, matchAll iterator)
// rather than engine correctness.

TEST(RegexAPITest, StructuredBindingsBasic) {
  constexpr auto re = compile<"(\\d+)-(\\w+)">();
  auto [full, num, word] = re.match("123-hello");
  EXPECT_EQ(full, "123-hello");
  EXPECT_EQ(num, "123");
  EXPECT_EQ(word, "hello");
}

TEST(RegexAPITest, StructuredBindingsFailedMatch) {
  constexpr auto re = compile<"(\\d+)-(\\w+)">();
  auto [full, num, word] = re.match("no-match-here!!!");
  EXPECT_TRUE(full.empty());
  EXPECT_TRUE(num.empty());
  EXPECT_TRUE(word.empty());
}

TEST(RegexAPITest, StructuredBindingsSingleGroup) {
  constexpr auto re = compile<"(\\w+)">();
  auto [full, word] = re.match("hello");
  EXPECT_EQ(full, "hello");
  EXPECT_EQ(word, "hello");
}

TEST(RegexAPITest, StructuredBindingsZeroGroups) {
  constexpr auto re = compile<"hello">();
  auto [full] = re.match("hello");
  EXPECT_EQ(full, "hello");
}

TEST(RegexAPITest, StructuredBindingsWithSearch) {
  constexpr auto re = compile<"(\\d+)">();
  auto [full, digits] = re.search("abc 42 def");
  EXPECT_EQ(full, "42");
  EXPECT_EQ(digits, "42");
}

TEST(RegexAPITest, StructuredBindingsDateParsing) {
  constexpr auto re = compile<"(\\d{4})-(\\d{2})-(\\d{2})">();
  auto [full, year, month, day] = re.match("2026-03-23");
  EXPECT_EQ(year, "2026");
  EXPECT_EQ(month, "03");
  EXPECT_EQ(day, "23");
}

TEST(RegexAPITest, FreestandingMatch) {
  auto m = folly::regex::match<"(\\d+)-(\\w+)">("123-hello");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[1], "123");
  EXPECT_EQ(m[2], "hello");
}

TEST(RegexAPITest, FreestandingSearch) {
  auto m = folly::regex::search<"\\d+">("abc 42 def");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "42");
}

TEST(RegexAPITest, FreestandingTest) {
  EXPECT_TRUE(folly::regex::test<"\\d+">("abc 42 def"));
  EXPECT_FALSE(folly::regex::test<"\\d+">("no digits"));
}

TEST(RegexAPITest, FreestandingWithFlags) {
  auto m = folly::regex::match<"hello", Flags::None>("hello");
  EXPECT_TRUE(m);
}

TEST(RegexAPITest, FreestandingStructuredBindings) {
  auto [full, num] = folly::regex::match<"(\\d+)">("42");
  EXPECT_EQ(num, "42");
}

TEST(RegexAPITest, MatchAllMultipleMatches) {
  constexpr auto re = compile<"\\d+">();
  std::vector<std::string> matches;
  for (auto m : re.matchAll("12 abc 34 def 56")) {
    matches.emplace_back(m[0]);
  }
  std::vector<std::string> expected{"12", "34", "56"};
  EXPECT_EQ(matches, expected);
}

TEST(RegexAPITest, MatchAllNoMatches) {
  constexpr auto re = compile<"\\d+">();
  std::vector<std::string> matches;
  for (auto m : re.matchAll("no digits")) {
    matches.emplace_back(m[0]);
  }
  EXPECT_TRUE(matches.empty());
}

TEST(RegexAPITest, MatchAllSingleMatch) {
  constexpr auto re = compile<"\\d+">();
  std::vector<std::string> matches;
  for (auto m : re.matchAll("abc42def")) {
    matches.emplace_back(m[0]);
  }
  std::vector<std::string> expected{"42"};
  EXPECT_EQ(matches, expected);
}

TEST(RegexAPITest, MatchAllFreestanding) {
  std::vector<std::string> matches;
  for (auto m : folly::regex::matchAll<"\\d+">("1 2 3")) {
    matches.emplace_back(m[0]);
  }
  std::vector<std::string> expected{"1", "2", "3"};
  EXPECT_EQ(matches, expected);
}

TEST(RegexAPITest, MatchAllWithCaptures) {
  constexpr auto re = compile<"(\\d+)-(\\w+)">();
  std::vector<std::string> firsts, seconds;
  for (auto m : re.matchAll("12-ab 34-cd")) {
    firsts.emplace_back(m[1]);
    seconds.emplace_back(m[2]);
  }
  std::vector<std::string> expectedFirsts{"12", "34"};
  std::vector<std::string> expectedSeconds{"ab", "cd"};
  EXPECT_EQ(firsts, expectedFirsts);
  EXPECT_EQ(seconds, expectedSeconds);
}

TEST(RegexAPITest, PrefixIterator) {
  constexpr auto re = compile<"hello">();
  std::vector<std::string> matches;
  for (auto m : re.matchAll("hello hello hello")) {
    matches.emplace_back(m[0]);
  }
  EXPECT_EQ(matches.size(), 3u);
  for (const auto& m : matches) {
    EXPECT_EQ(m, "hello");
  }
}

TEST(RegexAPITest, BasicUDL) {
  using namespace folly::regex::literals;
  constexpr auto re = "\\d+"_re;
  EXPECT_TRUE(re.match("123"));
  EXPECT_FALSE(re.match("abc"));
}

TEST(RegexAPITest, UDLSearch) {
  using namespace folly::regex::literals;
  constexpr auto re = "(\\w+)@(\\w+)"_re;
  auto m = re.search("contact user@host please");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[1], "user");
  EXPECT_EQ(m[2], "host");
}
