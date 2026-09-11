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

#include <folly/regex/Regex.h>
#include <folly/regex/detail/Parser.h>
#include <folly/regex/test/AstTestHelpers.h>
#include <folly/regex/test/CrossEngineTestHelpers.h>

#include <folly/portability/GTest.h>

using namespace folly::regex;
using namespace folly::regex::testing;
using detail::NodeKind;
using detail::parse;
using detail::ParseErrorMode;

// =====================================================================
// Behavioral tests: basic named group access via m["name"].
// =====================================================================

TEST(RegexNamedGroupTest, SinglePcreSyntaxNamedGroup) {
  auto m = expectMatchCapturesAgree<R"((?<word>\w+))">("hello");
  ASSERT_TRUE(bool(m));
  EXPECT_EQ(m["word"], "hello");
  // Numeric index still works for the same group.
  EXPECT_EQ(m[1], "hello");
  EXPECT_EQ(m[0], "hello");
}

TEST(RegexNamedGroupTest, SinglePythonSyntaxNamedGroup) {
  auto m = expectMatchCapturesAgree<R"((?P<word>\w+))">("hello");
  ASSERT_TRUE(bool(m));
  EXPECT_EQ(m["word"], "hello");
  EXPECT_EQ(m[1], "hello");
}

TEST(RegexNamedGroupTest, MultipleNamedGroups) {
  auto m = expectMatchCapturesAgree<
      R"((?<year>\d{4})-(?<month>\d{2})-(?<day>\d{2}))">("2026-04-18");
  ASSERT_TRUE(bool(m));
  EXPECT_EQ(m["year"], "2026");
  EXPECT_EQ(m["month"], "04");
  EXPECT_EQ(m["day"], "18");
  // Numeric indices still match.
  EXPECT_EQ(m[1], "2026");
  EXPECT_EQ(m[2], "04");
  EXPECT_EQ(m[3], "18");
}

TEST(RegexNamedGroupTest, MixedNamedAndUnnamed) {
  // Named groups share the same group_id sequence as unnamed ones.
  auto m = expectMatchCapturesAgree<R"((a)(?<name>b)(c))">("abc");
  ASSERT_TRUE(bool(m));
  EXPECT_EQ(m[1], "a");
  EXPECT_EQ(m["name"], "b");
  EXPECT_EQ(m[2], "b");
  EXPECT_EQ(m[3], "c");
}

TEST(RegexNamedGroupTest, NestedNamedGroups) {
  auto m = expectMatchCapturesAgree<R"((?<outer>foo (?<inner>bar) baz))">(
      "foo bar baz");
  ASSERT_TRUE(bool(m));
  EXPECT_EQ(m["outer"], "foo bar baz");
  EXPECT_EQ(m["inner"], "bar");
  EXPECT_EQ(m[1], "foo bar baz");
  EXPECT_EQ(m[2], "bar");
}

TEST(RegexNamedGroupTest, SearchWithNamedGroups) {
  // Use input with a single match so forward and reverse engines agree.
  auto m = expectSearchCapturesAgree<R"((?<word>\w+))">("  hello  ");
  ASSERT_TRUE(bool(m));
  EXPECT_EQ(m["word"], "hello");
}

TEST(RegexNamedGroupTest, NonexistentNameReturnsEmpty) {
  auto m = Regex<R"((?<word>\w+))">::match("hello");
  ASSERT_TRUE(bool(m));
  EXPECT_EQ(m["nonexistent"], std::string_view{});
}

TEST(RegexNamedGroupTest, EmptyNameMapPatternStillSupportsNameLookup) {
  // A pattern with no named groups still has the operator[](string_view)
  // overload — it always returns an empty string_view.
  auto m = Regex<R"((\w+))">::match("hello");
  ASSERT_TRUE(bool(m));
  EXPECT_EQ(m[1], "hello");
  EXPECT_EQ(m["any_name"], std::string_view{});
}

TEST(RegexNamedGroupTest, StructuredBindingsStillWork) {
  // The MatchResult template parameter change must not break structured
  // bindings. Three captures + the full-match slot = 4 elements.
  auto m = Regex<R"((?<year>\d{4})-(?<month>\d{2})-(?<day>\d{2}))">::match(
      "2026-04-18");
  ASSERT_TRUE(bool(m));
  auto [full, y, mo, d] = m;
  EXPECT_EQ(full, "2026-04-18");
  EXPECT_EQ(y, "2026");
  EXPECT_EQ(mo, "04");
  EXPECT_EQ(d, "18");
}

// =====================================================================
// Case-sensitivity of names (matching Perl): names are byte-exact.
// =====================================================================

TEST(RegexNamedGroupTest, NameLookupIsCaseSensitive) {
  auto m = Regex<R"((?<word>\w+))">::match("hello");
  ASSERT_TRUE(bool(m));
  EXPECT_EQ(m["word"], "hello");
  // Wrong case → not found → empty.
  EXPECT_EQ(m["Word"], std::string_view{});
  EXPECT_EQ(m["WORD"], std::string_view{});
}

TEST(RegexNamedGroupTest, CaseDifferingNamesAreDistinct) {
  // (?<word>...) (?<Word>...) is allowed — they're separate groups.
  auto m =
      expectMatchCapturesAgree<R"((?<word>\w+) (?<Word>\w+))">("hello world");
  ASSERT_TRUE(bool(m));
  EXPECT_EQ(m["word"], "hello");
  EXPECT_EQ(m["Word"], "world");
  EXPECT_EQ(m[1], "hello");
  EXPECT_EQ(m[2], "world");
}

TEST(RegexNamedGroupTest, BackrefViaKAngleBracket) {
  auto m = Regex<R"((?<word>\w+) \k<word>)">::match("hello hello");
  ASSERT_TRUE(bool(m));
  EXPECT_EQ(m["word"], "hello");
}

TEST(RegexNamedGroupTest, BackrefViaKAngleBracketMismatch) {
  auto m = Regex<R"((?<word>\w+) \k<word>)">::match("hello world");
  EXPECT_FALSE(bool(m));
}

TEST(RegexNamedGroupTest, BackrefViaPythonSyntax) {
  auto m = Regex<R"((?P<word>\w+) (?P=word))">::match("abc abc");
  ASSERT_TRUE(bool(m));
  EXPECT_EQ(m["word"], "abc");
}

// =====================================================================
// Named backreferences: \k<name> and (?P=name).
// =====================================================================

TEST(RegexNamedGroupTest, BackrefPythonSyntaxMismatch) {
  auto m = Regex<R"((?P<word>\w+) (?P=word))">::match("abc def");
  EXPECT_FALSE(bool(m));
}

TEST(RegexNamedGroupTest, NamedAndNumberedBackrefEquivalent) {
  // \k<name> resolves to the same group_id as the corresponding \N.
  auto mNamed = Regex<R"((?<x>a)\k<x>)">::match("aa");
  auto mNumber = Regex<R"((?<x>a)\1)">::match("aa");
  EXPECT_EQ(bool(mNamed), bool(mNumber));
  EXPECT_TRUE(bool(mNamed));
}

TEST(RegexNamedGroupTest, NamedBackrefSelfReference) {
  // Self-reference inside the same group: backref to a group that's
  // currently being captured. Inner \k<x> resolves to group_id for "x".
  auto m = Regex<R"((?<x>a)\k<x>)">::match("aa");
  EXPECT_TRUE(bool(m));
  auto m2 = Regex<R"((?<x>a)\k<x>)">::match("ab");
  EXPECT_FALSE(bool(m2));
}

// =====================================================================
// AST structure tests.
// =====================================================================

TEST(RegexNamedGroupTest, NamedGroupCountReflected) {
  constexpr auto& ast = Regex<R"((?<foo>a)(?<bar>b))">::parsed_;
  static_assert(ast.named_group_count == 2);
  static_assert(ast.group_count == 2);
}

TEST(RegexNamedGroupTest, NamedGroupArraySorted) {
  // After compact(), entries are sorted by name. "bar" < "foo".
  constexpr auto& ast = Regex<R"((?<foo>a)(?<bar>b))">::parsed_;
  static_assert(ast.named_group_count == 2);
  static_assert(ast.named_groups[0].nameView() == "bar");
  static_assert(ast.named_groups[0].group_id == 2);
  static_assert(ast.named_groups[1].nameView() == "foo");
  static_assert(ast.named_groups[1].group_id == 1);
}

TEST(RegexNamedGroupTest, NamedGroupIdsAreSequential) {
  // (?<x>a) is the only group and gets group_id=1.
  constexpr auto& ast = Regex<R"((?<x>a))">::parsed_;
  static_assert(ast.named_group_count == 1);
  static_assert(ast.named_groups[0].nameView() == "x");
  static_assert(ast.named_groups[0].group_id == 1);
}

TEST(RegexNamedGroupTest, MixedNamedAndUnnamedShareIdSequence) {
  // (a)(?<x>b)(c) — group_ids are 1, 2, 3.
  constexpr auto& ast = Regex<R"((a)(?<x>b)(c))">::parsed_;
  static_assert(ast.named_group_count == 1);
  static_assert(ast.group_count == 3);
  static_assert(ast.named_groups[0].nameView() == "x");
  static_assert(ast.named_groups[0].group_id == 2);
}

TEST(RegexNamedGroupTest, PatternWithoutNamedGroupsHasZeroCount) {
  constexpr auto& ast = Regex<R"((a)(b))">::parsed_;
  static_assert(ast.named_group_count == 0);
  static_assert(ast.group_count == 2);
}

TEST(RegexNamedGroupTest, PythonAndPcreSyntaxProduceEquivalentAst) {
  constexpr auto& astPcre = Regex<R"((?<a>x)(?<b>y))">::parsed_;
  constexpr auto& astPython = Regex<R"((?P<a>x)(?P<b>y))">::parsed_;
  static_assert(astPcre.named_group_count == astPython.named_group_count);
  static_assert(astPcre.group_count == astPython.group_count);
}

TEST(RegexNamedGroupTest, NamedBackrefIsRegularBackrefNode) {
  // \k<x> with no CI flag produces a Backref node, not a new node kind.
  // Use \w+ for the capture so the optimizer doesn't fold the backref
  // away (when both capture and backref are fixed literals, the optimizer
  // can rewrite the backref as a literal comparison).
  constexpr auto& ast = Regex<R"((?<x>\w+)\k<x>)">::parsed_;
  static_assert(countNodesOfKind(ast, NodeKind::Backref) == 1);
  static_assert(countNodesOfKind(ast, NodeKind::CaseInsensitiveBackref) == 0);
}

TEST(RegexNamedGroupTest, PythonNamedBackrefProducesBackrefNode) {
  constexpr auto& ast = Regex<R"((?P<x>\w+)(?P=x))">::parsed_;
  static_assert(countNodesOfKind(ast, NodeKind::Backref) == 1);
  static_assert(countNodesOfKind(ast, NodeKind::CaseInsensitiveBackref) == 0);
}

// =====================================================================
// Error tests: malformed names and undefined references.
// =====================================================================

TEST(RegexNamedGroupTest, DuplicateNameErrors) {
  auto result = parse<18, ParseErrorMode::RuntimeReport>(
      std::string_view("(?<foo>a)(?<foo>b)", 18));
  EXPECT_FALSE(result.valid);
}

TEST(RegexNamedGroupTest, CaseDifferingNamesNotDuplicate) {
  // (?<foo>a)(?<Foo>b) is valid — names are case-sensitive.
  auto result = parse<18, ParseErrorMode::RuntimeReport>(
      std::string_view("(?<foo>a)(?<Foo>b)", 18));
  EXPECT_TRUE(result.valid);
}

TEST(RegexNamedGroupTest, InvalidNameStartCharErrors) {
  // First char of name must be [a-zA-Z_], not a digit.
  auto result = parse<11, ParseErrorMode::RuntimeReport>(
      std::string_view("(?<1name>a)", 11));
  EXPECT_FALSE(result.valid);
}

TEST(RegexNamedGroupTest, EmptyNameErrors) {
  auto result =
      parse<6, ParseErrorMode::RuntimeReport>(std::string_view("(?<>a)", 6));
  EXPECT_FALSE(result.valid);
}

TEST(RegexNamedGroupTest, EmptyPythonNameErrors) {
  auto result =
      parse<7, ParseErrorMode::RuntimeReport>(std::string_view("(?P<>a)", 7));
  EXPECT_FALSE(result.valid);
}

TEST(RegexNamedGroupTest, MissingClosingAngleErrors) {
  // (?<name without closing > — the body parser will eventually fail.
  auto result =
      parse<7, ParseErrorMode::RuntimeReport>(std::string_view("(?<name", 7));
  EXPECT_FALSE(result.valid);
}

TEST(RegexNamedGroupTest, InvalidNameContinuationErrors) {
  // '-' is not a valid name continuation char.
  auto result =
      parse<9, ParseErrorMode::RuntimeReport>(std::string_view("(?<a-b>x)", 9));
  EXPECT_FALSE(result.valid);
}

TEST(RegexNamedGroupTest, NamedBackrefToUndefinedGroupErrors) {
  auto result = parse<13, ParseErrorMode::RuntimeReport>(
      std::string_view("\\k<undefined>", 13));
  EXPECT_FALSE(result.valid);
}

TEST(RegexNamedGroupTest, ForwardNamedBackrefErrors) {
  // \k<x> appears BEFORE (?<x>a). At the point of \k<x>, "x" is not
  // yet registered, so this must error.
  auto result = parse<12, ParseErrorMode::RuntimeReport>(
      std::string_view("\\k<x>(?<x>a)", 12));
  EXPECT_FALSE(result.valid);
}

TEST(RegexNamedGroupTest, PythonBackrefToUndefinedGroupErrors) {
  auto result = parse<14, ParseErrorMode::RuntimeReport>(
      std::string_view("(?P=undefined)", 14));
  EXPECT_FALSE(result.valid);
}

TEST(RegexNamedGroupTest, KEscapeMissingAngleBracketErrors) {
  // \k must be followed by '<' for named backref. \k followed by
  // anything else is an error.
  auto result =
      parse<3, ParseErrorMode::RuntimeReport>(std::string_view("\\kx", 3));
  EXPECT_FALSE(result.valid);
}

TEST(RegexNamedGroupTest, NameTooLongErrors) {
  // 32-char name exceeds the 31-char limit.
  auto pattern = std::string("(?<") + std::string(32, 'a') + std::string(">x)");
  auto result =
      parse<100, ParseErrorMode::RuntimeReport>(std::string_view(pattern));
  EXPECT_FALSE(result.valid);
}

TEST(RegexNamedGroupTest, NameAtExactlyMaxLengthAllowed) {
  // 31-char name is at the limit and should parse.
  auto pattern = std::string("(?<") + std::string(31, 'a') + std::string(">x)");
  auto result =
      parse<100, ParseErrorMode::RuntimeReport>(std::string_view(pattern));
  EXPECT_TRUE(result.valid);
}

TEST(RegexNamedGroupTest, NameWithDigitsAndUnderscore) {
  auto m = Regex<R"((?<my_name_42>\d+))">::match("123");
  ASSERT_TRUE(bool(m));
  EXPECT_EQ(m["my_name_42"], "123");
}

TEST(RegexNamedGroupTest, NameWithLeadingUnderscore) {
  auto m = Regex<R"((?<_priv>\w+))">::match("hello");
  ASSERT_TRUE(bool(m));
  EXPECT_EQ(m["_priv"], "hello");
}

// =====================================================================
// Many-group patterns to exercise the binary-search code path.
// =====================================================================

TEST(RegexNamedGroupTest, ManyNamedGroupsBinarySearch) {
  // 6 named groups exceeds the linear-search threshold (4), so the
  // map's find() uses binary search.
  auto m =
      Regex<R"((?<a>1)(?<b>2)(?<c>3)(?<d>4)(?<e>5)(?<f>6))">::match("123456");
  ASSERT_TRUE(bool(m));
  EXPECT_EQ(m["a"], "1");
  EXPECT_EQ(m["b"], "2");
  EXPECT_EQ(m["c"], "3");
  EXPECT_EQ(m["d"], "4");
  EXPECT_EQ(m["e"], "5");
  EXPECT_EQ(m["f"], "6");
  // Lookup of a missing name in a populated map.
  EXPECT_EQ(m["g"], std::string_view{});
}

TEST(RegexNamedGroupTest, CaseInsensitiveFlagDoesNotAffectNameLookup) {
  // /i affects character matching only, not pattern-level identifiers.
  auto m = Regex<R"((?<w>\w+))", Flags::CaseInsensitive>::match("HELLO");
  ASSERT_TRUE(bool(m));
  EXPECT_EQ(m["w"], "HELLO");
  // Different-case lookup still returns empty.
  EXPECT_EQ(m["W"], std::string_view{});
}

TEST(RegexNamedGroupTest, NamedBackrefWithCaseInsensitive) {
  auto m =
      Regex<R"((?<w>\w+) \k<w>)", Flags::CaseInsensitive>::match("Hello HELLO");
  ASSERT_TRUE(bool(m));
  EXPECT_EQ(m["w"], "Hello");
}

TEST(RegexNamedGroupTest, NamedBackrefWithCaseInsensitiveProducesCiNode) {
  constexpr auto& ast =
      Regex<R"((?<x>\w+)\k<x>)", Flags::CaseInsensitive>::parsed_;
  static_assert(countNodesOfKind(ast, NodeKind::CaseInsensitiveBackref) == 1);
  static_assert(countNodesOfKind(ast, NodeKind::Backref) == 0);
}

TEST(RegexNamedGroupTest, NamedBackrefRespectsScopedCaseInsensitive) {
  // (?i)(?<w>\w+) \k<w>) — both capture and backref are inside (?i).
  // With case-insensitive, "Hello HELLO" should match.
  auto m = Regex<R"((?i)(?<w>\w+) \k<w>)">::match("Hello HELLO");
  ASSERT_TRUE(bool(m));
  EXPECT_EQ(m["w"], "Hello");
}
