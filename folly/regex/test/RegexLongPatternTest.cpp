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

#include <folly/portability/GTest.h>
#include <folly/regex/Regex.h>

using namespace folly::regex;

TEST(RegexLongPatternTest, Match65) {
  // 2*26 + 13 = 65
  constexpr auto re = compile<
      "abcdefghijklmnopqrstuvwxyz"
      "abcdefghijklmnopqrstuvwxyz"
      "abcdefghijklm",
      Flags::ForceBacktracking>();
  std::string s =
      "abcdefghijklmnopqrstuvwxyz"
      "abcdefghijklmnopqrstuvwxyz"
      "abcdefghijklm";
  ASSERT_EQ(s.size(), 65u);
  EXPECT_TRUE(re.match(s));
}

TEST(RegexLongPatternTest, Match132) {
  // 5*26 + 2 = 132
  constexpr auto re = compile<
      "abcdefghijklmnopqrstuvwxyz"
      "abcdefghijklmnopqrstuvwxyz"
      "abcdefghijklmnopqrstuvwxyz"
      "abcdefghijklmnopqrstuvwxyz"
      "abcdefghijklmnopqrstuvwxyz"
      "ab",
      Flags::ForceBacktracking>();
  std::string s;
  for (int i = 0; i < 5; ++i) {
    s += "abcdefghijklmnopqrstuvwxyz";
  }
  s += "ab";
  ASSERT_EQ(s.size(), 132u);
  EXPECT_TRUE(re.match(s));
}

TEST(RegexLongPatternTest, Match204) {
  // 7*26 + 22 = 204
  constexpr auto re = compile<
      "abcdefghijklmnopqrstuvwxyz"
      "abcdefghijklmnopqrstuvwxyz"
      "abcdefghijklmnopqrstuvwxyz"
      "abcdefghijklmnopqrstuvwxyz"
      "abcdefghijklmnopqrstuvwxyz"
      "abcdefghijklmnopqrstuvwxyz"
      "abcdefghijklmnopqrstuvwxyz"
      "abcdefghijklmnopqrstuv",
      Flags::ForceBacktracking>();
  std::string s;
  for (int i = 0; i < 7; ++i) {
    s += "abcdefghijklmnopqrstuvwxyz";
  }
  s += "abcdefghijklmnopqrstuv";
  ASSERT_EQ(s.size(), 204u);
  EXPECT_TRUE(re.match(s));
}

TEST(RegexLongPatternTest, Match302) {
  // 11*26 + 16 = 302
  constexpr auto re = compile<
      "abcdefghijklmnopqrstuvwxyz"
      "abcdefghijklmnopqrstuvwxyz"
      "abcdefghijklmnopqrstuvwxyz"
      "abcdefghijklmnopqrstuvwxyz"
      "abcdefghijklmnopqrstuvwxyz"
      "abcdefghijklmnopqrstuvwxyz"
      "abcdefghijklmnopqrstuvwxyz"
      "abcdefghijklmnopqrstuvwxyz"
      "abcdefghijklmnopqrstuvwxyz"
      "abcdefghijklmnopqrstuvwxyz"
      "abcdefghijklmnopqrstuvwxyz"
      "abcdefghijklmnop",
      Flags::ForceBacktracking>();
  std::string expected;
  for (int i = 0; i < 11; ++i) {
    expected += "abcdefghijklmnopqrstuvwxyz";
  }
  expected += "abcdefghijklmnop";
  ASSERT_EQ(expected.size(), 302u);
  EXPECT_TRUE(re.match(expected));
  EXPECT_FALSE(re.match(std::string(302, 'x')));
}
