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

#include <folly/regex/test/AstTestHelpers.h>
#include <folly/regex/test/CrossEngineTestHelpers.h>

#include <folly/portability/GTest.h>

using namespace folly::regex;
using namespace folly::regex::testing;
using detail::AnchorKind;
using detail::NodeKind;
using detail::RepeatMode;

// ===== Literal Prefix Stripping =====

TEST(RegexCrossEngineOptTest, LiteralPrefixStrippingMatch) {
  // "hello" → entire pattern is a literal prefix, root becomes Empty
  constexpr auto& ast = Regex<"hello">::parsed_;
  static_assert(ast.prefix_len == 5);
  static_assert(prefixEquals(ast, "hello"));
  static_assert(rootKind(ast) == NodeKind::Empty);

  expectMatchAllEngines<"hello">("hello", true);
  expectMatchAllEngines<"hello">("hell", false);
  expectMatchAllEngines<"hello">("helloo", false);
  expectMatchAllEngines<"hello">("", false);
}

TEST(RegexCrossEngineOptTest, PrefixWithRepeat) {
  // Root is Repeat — no literal prefix can be extracted
  constexpr auto& ast = Regex<"a+">::parsed_;
  static_assert(ast.prefix_len == 0);
  static_assert(rootKind(ast) == NodeKind::Repeat);

  expectMatchAllEngines<"a+">("a", true);
  expectMatchAllEngines<"a+">("aaa", true);
  expectMatchAllEngines<"a+">("", false);
}

TEST(RegexCrossEngineOptTest, LiteralPrefixWithSuffix) {
  // "abc.*xyz" → prefix="abc" extracted; trailing "xyz" remains in AST.
  constexpr auto& ast = Regex<"abc.*xyz">::parsed_;
  static_assert(ast.prefix_len == 3);
  static_assert(prefixEquals(ast, "abc"));

  expectMatchAllEngines<"abc.*xyz">("abcxyz", true);
  expectMatchAllEngines<"abc.*xyz">("abc123xyz", true);
  expectMatchAllEngines<"abc.*xyz">("abcxy", false);
  expectMatchAllEngines<"abc.*xyz">("bc123xyz", false);
}

TEST(RegexCrossEngineOptTest, SuffixFastRejectMatch) {
  // "abc" → entire pattern is prefix, root is Empty
  constexpr auto& ast = Regex<"abc">::parsed_;
  static_assert(ast.prefix_len == 3);
  static_assert(prefixEquals(ast, "abc"));
  static_assert(rootKind(ast) == NodeKind::Empty);

  expectMatchAllEngines<"abc">("abc", true);
  expectMatchAllEngines<"abc">("abd", false);
  expectMatchAllEngines<"abc">("xbc", false);
}

TEST(RegexCrossEngineOptTest, PrefixWithCapturingGroup) {
  // "(abc)def" → prefix "abcdef" extracted through capturing Group
  constexpr auto& ast = Regex<"(abc)def">::parsed_;
  static_assert(ast.prefix_len == 6);
  static_assert(prefixEquals(ast, "abcdef"));
  static_assert(ast.prefix_group_adjustment_count == 1);

  auto m = expectMatchCapturesAgree<"(abc)def">("abcdef");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "abcdef");
  EXPECT_EQ(m[1], "abc");
}

TEST(RegexCrossEngineOptTest, PrefixStrippedWithCaptures) {
  // "hello(world)" → prefix "helloworld" now extracted through capturing Group
  constexpr auto& ast = Regex<"hello(world)">::parsed_;
  static_assert(ast.prefix_len == 10);
  static_assert(prefixEquals(ast, "helloworld"));
  static_assert(ast.prefix_group_adjustment_count == 1);

  auto m = expectMatchCapturesAgree<"hello(world)">("helloworld");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "helloworld");
  EXPECT_EQ(m[1], "world");
}

TEST(RegexCrossEngineOptTest, PrefixThroughNonCapturingGroupPartial) {
  constexpr auto& ast = Regex<"(?:abc\\d+)def">::parsed_;
  static_assert(ast.prefix_len == 3);
  static_assert(prefixEquals(ast, "abc"));

  expectMatchAllEngines<"(?:abc\\d+)def">("abc123def", true);
  expectMatchAllEngines<"(?:abc\\d+)def">("abcdef", false);
}

TEST(RegexCrossEngineOptTest, PrefixFullyStrippedCapturingGroup) {
  constexpr auto& ast1 = Regex<"(abc)def">::parsed_;
  static_assert(ast1.prefix_len == 6);
  static_assert(prefixEquals(ast1, "abcdef"));
  static_assert(ast1.prefix_group_adjustment_count == 1);

  auto m1 = expectMatchCapturesAgree<"(abc)def">("abcdef");
  EXPECT_TRUE(m1);
  EXPECT_EQ(m1[0], "abcdef");
  EXPECT_EQ(m1[1], "abc");

  constexpr auto& ast2 = Regex<"(abc)\\d+">::parsed_;
  static_assert(ast2.prefix_len == 3);
  static_assert(prefixEquals(ast2, "abc"));
  static_assert(ast2.prefix_group_adjustment_count == 1);

  auto m2 = expectMatchCapturesAgree<"(abc)\\d+">("abc123");
  EXPECT_TRUE(m2);
  EXPECT_EQ(m2[0], "abc123");
  EXPECT_EQ(m2[1], "abc");

  constexpr auto& ast3 = Regex<"foo(bar)baz">::parsed_;
  static_assert(ast3.prefix_len == 9);
  static_assert(prefixEquals(ast3, "foobarbaz"));
  static_assert(ast3.prefix_group_adjustment_count == 1);

  auto m3 = expectMatchCapturesAgree<"foo(bar)baz">("foobarbaz");
  EXPECT_TRUE(m3);
  EXPECT_EQ(m3[0], "foobarbaz");
  EXPECT_EQ(m3[1], "bar");
}

TEST(RegexCrossEngineOptTest, PrefixPartiallyStrippedCapturingGroup) {
  constexpr auto& ast1 = Regex<"(abc\\d+)xyz">::parsed_;
  static_assert(ast1.prefix_len == 3);
  static_assert(prefixEquals(ast1, "abc"));
  static_assert(ast1.prefix_group_adjustment_count == 1);
  static_assert(hasNodeOfKind(ast1, NodeKind::Group));
  constexpr int grpIdx1 = findNodeOfKind(ast1, NodeKind::Group);
  static_assert(ast1.nodes[grpIdx1].capturing);

  auto m1 = expectMatchCapturesAgree<"(abc\\d+)xyz">("abc123xyz");
  EXPECT_TRUE(m1);
  EXPECT_EQ(m1[0], "abc123xyz");
  EXPECT_EQ(m1[1], "abc123");

  constexpr auto& ast2 = Regex<"(abcdef\\w+)!">::parsed_;
  static_assert(ast2.prefix_len == 6);
  static_assert(prefixEquals(ast2, "abcdef"));
  static_assert(ast2.prefix_group_adjustment_count == 1);
  static_assert(hasNodeOfKind(ast2, NodeKind::Group));

  auto m2 = expectMatchCapturesAgree<"(abcdef\\w+)!">("abcdefghij!");
  EXPECT_TRUE(m2);
  EXPECT_EQ(m2[0], "abcdefghij!");
  EXPECT_EQ(m2[1], "abcdefghij");
}

TEST(RegexCrossEngineOptTest, PrefixThroughNestedGroups) {
  constexpr auto& ast = Regex<"((abc)def)ghi">::parsed_;
  static_assert(ast.prefix_len == 9);
  static_assert(prefixEquals(ast, "abcdefghi"));
  static_assert(ast.prefix_group_adjustment_count == 2);

  auto m = expectMatchCapturesAgree<"((abc)def)ghi">("abcdefghi");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "abcdefghi");
  EXPECT_EQ(m[1], "abcdef");
  EXPECT_EQ(m[2], "abc");
}

TEST(RegexCrossEngineOptTest, PrefixBlockedByRepeat) {
  constexpr auto& ast = Regex<"(abc)+def">::parsed_;
  static_assert(ast.prefix_len == 0);

  expectMatchAllEngines<"(abc)+def">("abcdef", true);
  expectMatchAllEngines<"(abc)+def">("abcabcdef", true);
  expectMatchAllEngines<"(abc)+def">("def", false);
}

TEST(RegexCrossEngineOptTest, CapturesWithTrailingLiteral) {
  // (\d+)abc — captures preserved through trailing literal.
  auto m = expectMatchCapturesAgree<"(\\d+)abc">("123abc");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "123abc");
  EXPECT_EQ(m[1], "123");
}

TEST(RegexCrossEngineOptTest, MatchWithOverlappingTrailingLiteral) {
  // [a-z]+abc — trailing literal overlaps the preceding repeat; verify
  // match correctness.
  expectMatchAllEngines<"[a-z]+abc">("xyzabc", true);
  expectMatchAllEngines<"[a-z]+abc">("abc", false);
  expectMatchAllEngines<"[a-z]+abc">("xyz", false);
}

TEST(RegexCrossEngineOptTest, LiteralPrefixStrippingSearch) {
  constexpr auto& ast = Regex<"world">::parsed_;
  static_assert(ast.prefix_len == 5);
  static_assert(prefixEquals(ast, "world"));

  expectSearchAllEngines<"world">("hello world", true, "world");
}

TEST(RegexCrossEngineOptTest, LiteralPrefixSearchNotFound) {
  constexpr auto& ast = Regex<"xyz">::parsed_;
  static_assert(ast.prefix_len == 3);
  static_assert(prefixEquals(ast, "xyz"));

  expectSearchAllEngines<"xyz">("hello world", false);
}

TEST(RegexCrossEngineOptTest, LiteralPrefixSearchAtStart) {
  expectSearchAllEngines<"hello">("hello world", true, "hello");
}

TEST(RegexCrossEngineOptTest, SearchWithPrefixAndCaptures) {
  // "foo(bar)" → prefix "foobar" now extracted through capturing Group
  constexpr auto& ast = Regex<"foo(bar)">::parsed_;
  static_assert(ast.prefix_len == 6);
  static_assert(prefixEquals(ast, "foobar"));
  static_assert(ast.prefix_group_adjustment_count == 1);

  auto m = expectSearchCapturesAgree<"foo(bar)">("xxxfoobaryyyy");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "foobar");
  EXPECT_EQ(m[1], "bar");
}

TEST(RegexCrossEngineOptTest, TestWithPrefix) {
  expectTestAllEngines<"hello">("say hello there", true);
  expectTestAllEngines<"hello">("no match here", false);
}

TEST(RegexCrossEngineOptTest, PrefixWithAnchor) {
  // "world$" → "world" stripped as prefix, root is Anchor($)
  constexpr auto& ast = Regex<"world$">::parsed_;
  static_assert(ast.prefix_len == 5);
  static_assert(prefixEquals(ast, "world"));
  static_assert(rootKind(ast) == NodeKind::Anchor);

  expectSearchAllEngines<"world$">("hello world", true, "world");
  expectSearchAllEngines<"world$">("world hello", false);
}

// ===== Prefix Through Groups =====

TEST(RegexCrossEngineOptTest, PrefixThroughNonCapturingGroup) {
  constexpr auto& ast = Regex<"(?:abc)def">::parsed_;
  static_assert(ast.prefix_len == 6);
  static_assert(prefixEquals(ast, "abcdef"));
  static_assert(rootKind(ast) == NodeKind::Empty);
  static_assert(!hasNodeOfKind(ast, NodeKind::Group));

  expectMatchAllEngines<"(?:abc)def">("abcdef", true);
  expectMatchAllEngines<"(?:abc)def">("abcde", false);
  expectSearchAllEngines<"(?:abc)def">("xxxabcdefyyy", true, "abcdef");
}

TEST(RegexCrossEngineOptTest, SearchPrefixThroughCapturingGroup) {
  constexpr auto& ast = Regex<"(hello)world">::parsed_;
  static_assert(ast.prefix_len == 10);
  static_assert(prefixEquals(ast, "helloworld"));
  static_assert(ast.prefix_group_adjustment_count == 1);

  auto m = expectSearchCapturesAgree<"(hello)world">("say helloworld now");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "helloworld");
  EXPECT_EQ(m[1], "hello");
}
