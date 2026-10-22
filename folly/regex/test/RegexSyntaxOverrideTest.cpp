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

// Tests for syntax override semantics: Preset | Syntax_X adds an enable,
// Preset & ~Syntax_X removes one, and behavior flags compose with presets.

#include <folly/regex/Regex.h>
#include <folly/regex/detail/Parser.h>

#include <folly/portability/GTest.h>

using namespace folly::regex;
using namespace folly::regex::detail;

template <std::size_t N>
constexpr auto parseSyntax(const char (&pattern)[N], Flags syntaxFlags) {
  return parse<N - 1, ParseErrorMode::RuntimeReport>(
      std::string_view(pattern, N - 1), syntaxFlags);
}

// Adding a feature: SyntaxPreset_Re2 | Syntax_Lookaround accepts (?=...)
TEST(SyntaxOverrideTest, Re2PlusLookaround_AcceptsLookahead) {
  static constexpr auto r = parseSyntax(
      "(?=abc)", Flags::SyntaxPreset_Re2 | Flags::Syntax_Lookaround);
  EXPECT_TRUE(r.valid);
}

TEST(SyntaxOverrideTest, Re2PlusLookaround_AcceptsNegLookbehind) {
  static constexpr auto r = parseSyntax(
      "(?<!abc)", Flags::SyntaxPreset_Re2 | Flags::Syntax_Lookaround);
  EXPECT_TRUE(r.valid);
}

// Removing a feature: SyntaxPreset_Folly & ~Syntax_PossessiveQuantifier
// rejects a++
TEST(SyntaxOverrideTest, FollyMinusPossessive_RejectsPossessive) {
  static constexpr auto r = parseSyntax(
      "a++", Flags::SyntaxPreset_Folly & ~Flags::Syntax_PossessiveQuantifier);
  EXPECT_FALSE(r.valid);
}

// Removing a feature: SyntaxPreset_Pcre & ~Syntax_AtomicGroup rejects (?>...)
TEST(SyntaxOverrideTest, PcreMinusAtomic_RejectsAtomic) {
  static constexpr auto r = parseSyntax(
      "(?>abc)", Flags::SyntaxPreset_Pcre & ~Flags::Syntax_AtomicGroup);
  EXPECT_FALSE(r.valid);
}

// Mixing preset with behavior flag: SyntaxPreset_Re2 | CaseInsensitive
TEST(SyntaxOverrideTest, Re2PlusCaseInsensitive_Works) {
  constexpr auto re2ci = Flags::SyntaxPreset_Re2 | Flags::CaseInsensitive;
  // Should accept \d (Re2 enables shorthand classes)
  static constexpr auto r = parseSyntax("\\d", re2ci);
  EXPECT_TRUE(r.valid);
}

// End-to-end: Regex<> template with override
TEST(SyntaxOverrideTest, RegexTemplate_Re2PlusLookaround) {
  constexpr auto kFlags = Flags::SyntaxPreset_Re2 | Flags::Syntax_Lookaround;
  auto m = Regex<"(?=a)a", kFlags>::match("a");
  EXPECT_TRUE(m);
}

// Syntax-mask-default does NOT apply when explicit syntax bits are present
TEST(SyntaxOverrideTest, ExplicitSyntaxBitsSuppressDefault) {
  // Only Syntax_LazyQuantifier — \d falls through to literal 'd'.
  static constexpr auto r = parseSyntax("\\d", Flags::Syntax_LazyQuantifier);
  EXPECT_TRUE(r.valid)
      << "\\d should parse as literal 'd' without ShorthandClasses";
}

// Flags::None gets full Folly syntax via compilationFlags syntax-mask-default
TEST(SyntaxOverrideTest, FlagsNone_GetsFollySyntax) {
  static constexpr auto r = parseSyntax("(?>abc)", Flags::SyntaxPreset_Folly);
  EXPECT_TRUE(r.valid);
}
