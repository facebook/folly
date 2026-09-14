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

// Syntax-by-construct matrix tests: for each preset, verify that enabled
// constructs compile and disabled constructs fail with regex_parse_error.

#include <folly/regex/Regex.h>
#include <folly/regex/detail/Parser.h>

#include <folly/portability/GTest.h>

using namespace folly::regex;
using namespace folly::regex::detail;

// Helper: parse with RuntimeReport mode so we get valid/invalid instead of
// a compile-time exception. Syntax flags are passed directly — the parse<>
// template applies the syntax-mask-default via compilationFlags().
template <std::size_t N>
constexpr auto parseSyntax(const char (&pattern)[N], Flags syntaxFlags) {
  return parse<N - 1, ParseErrorMode::RuntimeReport>(
      std::string_view(pattern, N - 1), syntaxFlags);
}

// =====================================================================
// SyntaxPreset_Folly — everything enabled
// =====================================================================

TEST(SyntaxPresetTest, Folly_AcceptsAtomicGroup) {
  static constexpr auto r = parseSyntax("(?>abc)", Flags::SyntaxPreset_Folly);
  EXPECT_TRUE(r.valid);
}

TEST(SyntaxPresetTest, Folly_AcceptsLookahead) {
  static constexpr auto r = parseSyntax("(?=abc)", Flags::SyntaxPreset_Folly);
  EXPECT_TRUE(r.valid);
}

TEST(SyntaxPresetTest, Folly_AcceptsLookbehind) {
  static constexpr auto r = parseSyntax("(?<=abc)", Flags::SyntaxPreset_Folly);
  EXPECT_TRUE(r.valid);
}

TEST(SyntaxPresetTest, Folly_AcceptsPossessive) {
  static constexpr auto r = parseSyntax("a++", Flags::SyntaxPreset_Folly);
  EXPECT_TRUE(r.valid);
}

TEST(SyntaxPresetTest, Folly_AcceptsLazy) {
  static constexpr auto r = parseSyntax("a+?", Flags::SyntaxPreset_Folly);
  EXPECT_TRUE(r.valid);
}

TEST(SyntaxPresetTest, Folly_AcceptsNamedGroupAngle) {
  static constexpr auto r =
      parseSyntax("(?<name>abc)", Flags::SyntaxPreset_Folly);
  EXPECT_TRUE(r.valid);
}

TEST(SyntaxPresetTest, Folly_AcceptsNamedGroupPython) {
  static constexpr auto r =
      parseSyntax("(?P<name>abc)", Flags::SyntaxPreset_Folly);
  EXPECT_TRUE(r.valid);
}

TEST(SyntaxPresetTest, Folly_AcceptsNumericBackref) {
  static constexpr auto r = parseSyntax("(a)\\1", Flags::SyntaxPreset_Folly);
  EXPECT_TRUE(r.valid);
}

TEST(SyntaxPresetTest, Folly_AcceptsShorthandClasses) {
  static constexpr auto r = parseSyntax("\\d\\w\\s", Flags::SyntaxPreset_Folly);
  EXPECT_TRUE(r.valid);
}

TEST(SyntaxPresetTest, Folly_AcceptsWordBoundary) {
  static constexpr auto r =
      parseSyntax("\\bword\\b", Flags::SyntaxPreset_Folly);
  EXPECT_TRUE(r.valid);
}

TEST(SyntaxPresetTest, Folly_AcceptsBufferAnchors) {
  static constexpr auto r = parseSyntax("\\Aabc\\z", Flags::SyntaxPreset_Folly);
  EXPECT_TRUE(r.valid);
}

TEST(SyntaxPresetTest, Folly_AcceptsSingleByteEscape) {
  static constexpr auto r = parseSyntax("\\C", Flags::SyntaxPreset_Folly);
  EXPECT_TRUE(r.valid);
}

TEST(SyntaxPresetTest, Folly_AcceptsPosixCharClass) {
  static constexpr auto r =
      parseSyntax("[[:alpha:]]", Flags::SyntaxPreset_Folly);
  EXPECT_TRUE(r.valid);
}

TEST(SyntaxPresetTest, Folly_AcceptsInlineFlags) {
  static constexpr auto r = parseSyntax("(?i)abc", Flags::SyntaxPreset_Folly);
  EXPECT_TRUE(r.valid);
}

// =====================================================================
// SyntaxPreset_Re2 — no backrefs, no lookaround, no atomic, no possessive
// =====================================================================

TEST(SyntaxPresetTest, Re2_RejectsAtomicGroup) {
  static constexpr auto r = parseSyntax("(?>abc)", Flags::SyntaxPreset_Re2);
  EXPECT_FALSE(r.valid);
}

TEST(SyntaxPresetTest, Re2_RejectsLookahead) {
  static constexpr auto r = parseSyntax("(?=abc)", Flags::SyntaxPreset_Re2);
  EXPECT_FALSE(r.valid);
}

TEST(SyntaxPresetTest, Re2_RejectsNegLookahead) {
  static constexpr auto r = parseSyntax("(?!abc)", Flags::SyntaxPreset_Re2);
  EXPECT_FALSE(r.valid);
}

TEST(SyntaxPresetTest, Re2_RejectsLookbehind) {
  static constexpr auto r = parseSyntax("(?<=abc)", Flags::SyntaxPreset_Re2);
  EXPECT_FALSE(r.valid);
}

TEST(SyntaxPresetTest, Re2_RejectsPossessive) {
  static constexpr auto r = parseSyntax("a++", Flags::SyntaxPreset_Re2);
  EXPECT_FALSE(r.valid);
}

TEST(SyntaxPresetTest, Re2_ParsesNumericBackrefAsLiteral) {
  // Without Syntax_NumericBackref, \1 is treated as literal '1'.
  static constexpr auto r = parseSyntax("(a)\\1", Flags::SyntaxPreset_Re2);
  EXPECT_TRUE(r.valid);
}

TEST(SyntaxPresetTest, Re2_ParsesKBackrefAsLiteral) {
  // Without Syntax_KBackref, \k is treated as literal 'k'.
  // The pattern becomes (?<n>a)k<n> which is valid.
  static constexpr auto r = parseSyntax(
      "(?<n>a)\\k<n>", Flags::SyntaxPreset_Re2 | Flags::Syntax_NamedGroupAngle);
  EXPECT_TRUE(r.valid);
}

TEST(SyntaxPresetTest, Re2_AcceptsLazy) {
  static constexpr auto r = parseSyntax("a+?", Flags::SyntaxPreset_Re2);
  EXPECT_TRUE(r.valid);
}

TEST(SyntaxPresetTest, Re2_AcceptsNamedGroupAngle) {
  static constexpr auto r =
      parseSyntax("(?<name>abc)", Flags::SyntaxPreset_Re2);
  EXPECT_TRUE(r.valid);
}

TEST(SyntaxPresetTest, Re2_AcceptsNamedGroupPython) {
  static constexpr auto r =
      parseSyntax("(?P<name>abc)", Flags::SyntaxPreset_Re2);
  EXPECT_TRUE(r.valid);
}

TEST(SyntaxPresetTest, Re2_AcceptsShorthandClasses) {
  static constexpr auto r = parseSyntax("\\d\\w\\s", Flags::SyntaxPreset_Re2);
  EXPECT_TRUE(r.valid);
}

TEST(SyntaxPresetTest, Re2_AcceptsPosixCharClass) {
  static constexpr auto r = parseSyntax("[[:alpha:]]", Flags::SyntaxPreset_Re2);
  EXPECT_TRUE(r.valid);
}

TEST(SyntaxPresetTest, Re2_AcceptsInlineFlags) {
  static constexpr auto r = parseSyntax("(?i)abc", Flags::SyntaxPreset_Re2);
  EXPECT_TRUE(r.valid);
}

// =====================================================================
// SyntaxPreset_Python — no atomic, no possessive, no (?<>), no \g, no \C,
// no \x{}, no POSIX classes
// =====================================================================

TEST(SyntaxPresetTest, Python_RejectsAtomicGroup) {
  static constexpr auto r = parseSyntax("(?>abc)", Flags::SyntaxPreset_Python);
  EXPECT_FALSE(r.valid);
}

TEST(SyntaxPresetTest, Python_RejectsPossessive) {
  static constexpr auto r = parseSyntax("a++", Flags::SyntaxPreset_Python);
  EXPECT_FALSE(r.valid);
}

TEST(SyntaxPresetTest, Python_RejectsNamedGroupAngle) {
  static constexpr auto r =
      parseSyntax("(?<name>abc)", Flags::SyntaxPreset_Python);
  EXPECT_FALSE(r.valid);
}

TEST(SyntaxPresetTest, Python_ParsesPosixCharClassAsLiteral) {
  // Without Syntax_PosixCharClass, [:alpha:] is treated as literal chars
  // in the character class. The pattern [[:alpha:]] becomes a char class
  // containing ':', 'a', 'l', 'p', 'h' followed by literal ']'.
  static constexpr auto r =
      parseSyntax("[[:alpha:]]", Flags::SyntaxPreset_Python);
  EXPECT_TRUE(r.valid);
}

TEST(SyntaxPresetTest, Python_ParsesSingleByteEscapeAsLiteral) {
  // Without Syntax_SingleByteEscape, \C is treated as literal 'C'.
  static constexpr auto r = parseSyntax("\\C", Flags::SyntaxPreset_Python);
  EXPECT_TRUE(r.valid);
}

TEST(SyntaxPresetTest, Python_AcceptsLookahead) {
  static constexpr auto r = parseSyntax("(?=abc)", Flags::SyntaxPreset_Python);
  EXPECT_TRUE(r.valid);
}

TEST(SyntaxPresetTest, Python_AcceptsNamedGroupPython) {
  static constexpr auto r =
      parseSyntax("(?P<name>abc)", Flags::SyntaxPreset_Python);
  EXPECT_TRUE(r.valid);
}

TEST(SyntaxPresetTest, Python_AcceptsLazy) {
  static constexpr auto r = parseSyntax("a+?", Flags::SyntaxPreset_Python);
  EXPECT_TRUE(r.valid);
}

TEST(SyntaxPresetTest, Python_AcceptsShorthandClasses) {
  static constexpr auto r = parseSyntax("\\d", Flags::SyntaxPreset_Python);
  EXPECT_TRUE(r.valid);
}

TEST(SyntaxPresetTest, Python_AcceptsNumericBackref) {
  static constexpr auto r = parseSyntax("(a)\\1", Flags::SyntaxPreset_Python);
  EXPECT_TRUE(r.valid);
}

// =====================================================================
// SyntaxPreset_Ruby — drops NamedGroupPython and SingleByteEscape
// =====================================================================

TEST(SyntaxPresetTest, Ruby_RejectsNamedGroupPython) {
  static constexpr auto r =
      parseSyntax("(?P<name>abc)", Flags::SyntaxPreset_Ruby);
  EXPECT_FALSE(r.valid);
}

TEST(SyntaxPresetTest, Ruby_ParsesSingleByteEscapeAsLiteral) {
  // Without Syntax_SingleByteEscape, \C is treated as literal 'C'.
  static constexpr auto r = parseSyntax("\\C", Flags::SyntaxPreset_Ruby);
  EXPECT_TRUE(r.valid);
}

TEST(SyntaxPresetTest, Ruby_AcceptsNamedGroupAngle) {
  static constexpr auto r =
      parseSyntax("(?<name>abc)", Flags::SyntaxPreset_Ruby);
  EXPECT_TRUE(r.valid);
}

TEST(SyntaxPresetTest, Ruby_AcceptsAtomicGroup) {
  static constexpr auto r = parseSyntax("(?>abc)", Flags::SyntaxPreset_Ruby);
  EXPECT_TRUE(r.valid);
}

TEST(SyntaxPresetTest, Ruby_AcceptsPossessive) {
  static constexpr auto r = parseSyntax("a++", Flags::SyntaxPreset_Ruby);
  EXPECT_TRUE(r.valid);
}

TEST(SyntaxPresetTest, Ruby_AcceptsLookaround) {
  static constexpr auto r = parseSyntax("(?=abc)", Flags::SyntaxPreset_Ruby);
  EXPECT_TRUE(r.valid);
}

// =====================================================================
// End-to-end: Regex<> template with presets
// =====================================================================

TEST(SyntaxPresetTest, RegexTemplate_FollyDefault_AcceptsAll) {
  // Default Flags::None gets SyntaxPreset_Folly via compilationFlags.
  auto m = Regex<"(?>a+)b">::match("aaab");
  EXPECT_TRUE(m);
}

TEST(SyntaxPresetTest, RegexTemplate_Re2_AcceptsSimple) {
  auto m = Regex<"\\d+", Flags::SyntaxPreset_Re2>::match("123");
  EXPECT_TRUE(m);
}
