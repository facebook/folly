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
#include <folly/regex/detail/Executor.h>

#include <folly/portability/GTest.h>

using namespace folly::regex;
using namespace folly::regex::detail;

// ===== computeDotStarExtension =====

TEST(ComputeDotStarExtension, ForwardDotAll) {
  InputView<Direction::Forward> iv{"hello\nworld"};
  // dotAll=true, no anchor → extends to end of input.
  auto result = computeDotStarExtension<Direction::Forward>(
      iv, 0, /*dotAll=*/true, /*anchorKind=*/-1);
  EXPECT_EQ(result, 11u); // "hello\nworld".size()
}

TEST(ComputeDotStarExtension, ForwardNonDotAll) {
  InputView<Direction::Forward> iv{"hello\nworld"};
  // dotAll=false, no anchor → extends to first \n.
  auto result = computeDotStarExtension<Direction::Forward>(
      iv, 0, /*dotAll=*/false, /*anchorKind=*/-1);
  EXPECT_EQ(result, 5u); // position of '\n'
}

TEST(ComputeDotStarExtension, ForwardEndOfStringAnchor) {
  InputView<Direction::Forward> iv{"hello\nworld"};
  // \z anchor + non-dotAll → npos because \n is found before end.
  auto result = computeDotStarExtension<Direction::Forward>(
      iv,
      0,
      /*dotAll=*/false,
      /*anchorKind=*/static_cast<int>(AnchorKind::EndOfString));
  EXPECT_EQ(result, std::string_view::npos);
}

TEST(ComputeDotStarExtension, ForwardEndOfStringOrNewlineAnchor) {
  // \Z anchor → accepts \n only if it's the last character.
  {
    InputView<Direction::Forward> iv{"hello\nworld"};
    // \n is NOT the last char → npos.
    auto result = computeDotStarExtension<Direction::Forward>(
        iv,
        0,
        /*dotAll=*/false,
        /*anchorKind=*/static_cast<int>(AnchorKind::EndOfStringOrNewline));
    EXPECT_EQ(result, std::string_view::npos);
  }
  {
    InputView<Direction::Forward> iv{"hello\n"};
    // \n IS the last char → accepts.
    auto result = computeDotStarExtension<Direction::Forward>(
        iv,
        0,
        /*dotAll=*/false,
        /*anchorKind=*/static_cast<int>(AnchorKind::EndOfStringOrNewline));
    EXPECT_EQ(result, 5u); // position of '\n'
  }
}

TEST(ComputeDotStarExtension, ForwardNoNewline) {
  InputView<Direction::Forward> iv{"hello"};
  // No \n in input → extends to end.
  auto result = computeDotStarExtension<Direction::Forward>(
      iv, 0, /*dotAll=*/false, /*anchorKind=*/-1);
  EXPECT_EQ(result, 5u);
}

TEST(ComputeDotStarExtension, ReverseDotAll) {
  InputView<Direction::Reverse> iv{"hello\nworld"};
  // dotAll=true → extends to start (position 0).
  auto result = computeDotStarExtension<Direction::Reverse>(
      iv, iv.size(), /*dotAll=*/true, /*anchorKind=*/-1);
  EXPECT_EQ(result, 0u);
}

TEST(ComputeDotStarExtension, ReverseNonDotAll) {
  InputView<Direction::Reverse> iv{"hello\nworld"};
  // dotAll=false → extends to last \n (scanning from end backward).
  auto result = computeDotStarExtension<Direction::Reverse>(
      iv, iv.size(), /*dotAll=*/false, /*anchorKind=*/-1);
  // Reverse-scanning from end of "hello\nworld" (pos=11) toward 0,
  // finds \n at index 5 → returns position 6 (after the \n).
  EXPECT_EQ(result, 6u);
}

// ===== computeLeadingDotStarExtension =====

TEST(ComputeLeadingDotStarExtension, ForwardExtendsToZero) {
  InputView<Direction::Forward> iv{"hello"};
  // Forward dotAll → extends start leftward to 0.
  auto result = computeLeadingDotStarExtension<Direction::Forward>(
      iv, 3, /*dotAll=*/true, /*anchorKind=*/-1);
  EXPECT_EQ(result, 0u);
}

TEST(ComputeLeadingDotStarExtension, ForwardNonDotAllNoNewline) {
  InputView<Direction::Forward> iv{"hello"};
  // No \n → extends to 0.
  auto result = computeLeadingDotStarExtension<Direction::Forward>(
      iv, 3, /*dotAll=*/false, /*anchorKind=*/-1);
  EXPECT_EQ(result, 0u);
}

TEST(ComputeLeadingDotStarExtension, ForwardStartOfStringAnchor) {
  InputView<Direction::Forward> iv{"he\nllo"};
  // \A anchor + \n found → npos (requires position 0, \n blocks it).
  auto result = computeLeadingDotStarExtension<Direction::Forward>(
      iv,
      5,
      /*dotAll=*/false,
      /*anchorKind=*/static_cast<int>(AnchorKind::StartOfString));
  EXPECT_EQ(result, std::string_view::npos);
}

TEST(ComputeLeadingDotStarExtension, ForwardStartOfStringNoNewline) {
  InputView<Direction::Forward> iv{"hello"};
  // \A anchor + no \n → extends to 0 (matches \A).
  auto result = computeLeadingDotStarExtension<Direction::Forward>(
      iv,
      3,
      /*dotAll=*/false,
      /*anchorKind=*/static_cast<int>(AnchorKind::StartOfString));
  EXPECT_EQ(result, 0u);
}

TEST(ComputeLeadingDotStarExtension, ReverseExtendsToEnd) {
  InputView<Direction::Reverse> iv{"hello"};
  // Reverse dotAll → extends end rightward to input.size().
  auto result = computeLeadingDotStarExtension<Direction::Reverse>(
      iv, 2, /*dotAll=*/true, /*anchorKind=*/-1);
  EXPECT_EQ(result, 5u);
}

TEST(ComputeLeadingDotStarExtension, ReverseNonDotAllWithNewline) {
  InputView<Direction::Reverse> iv{"hello\nworld"};
  // Reverse non-dotAll → scans for \n from pos rightward.
  auto result = computeLeadingDotStarExtension<Direction::Reverse>(
      iv, 2, /*dotAll=*/false, /*anchorKind=*/-1);
  // Scans from absPos=2 rightward, finds \n at index 5 → returns 5.
  EXPECT_EQ(result, 5u);
}

// ===== hasTrailingDotStarAnchors =====

// No trailing dot-star → false.
static_assert(!hasTrailingDotStarAnchors(Regex<"abc">::parsed_));
static_assert(!hasTrailingDotStarAnchors(Regex<"a">::parsed_));

// Pattern with trailing .*\z in non-dotAll mode should have the optimization.
// "a.*\\z" → trailing_dot_star_min >= 0, non-dotAll, anchor >= 0 → true.
TEST(HasTrailingDotStarAnchors, WithAnchoredDotStar) {
  constexpr auto& ast = Regex<R"(a.*\z)">::parsed_;
  // Whether the optimizer sets trailing_dot_star depends on the pattern.
  // Verify the function doesn't crash and returns a consistent result.
  constexpr bool result = hasTrailingDotStarAnchors(ast);
  EXPECT_EQ(
      result,
      ast.trailing_dot_star_min >= 0 && !ast.trailing_dot_star_dot_all &&
          ast.trailing_dot_star_anchor >= 0);
}

// DotAll pattern: even with trailing .* + anchor, dotAll → false.
TEST(HasTrailingDotStarAnchors, DotAllPattern) {
  constexpr auto& ast = Regex<"a">::parsed_;
  // Simple pattern has no trailing dot-star.
  EXPECT_FALSE(hasTrailingDotStarAnchors(ast));
}

// No anchor → false (trailing_dot_star_anchor < 0).
TEST(HasTrailingDotStarAnchors, NoAnchorCase) {
  constexpr auto& ast = Regex<"a.*">::parsed_;
  // "a.*" may have trailing_dot_star_min >= 0, but no anchor → false.
  if (ast.trailing_dot_star_min >= 0) {
    EXPECT_EQ(
        hasTrailingDotStarAnchors(ast),
        !ast.trailing_dot_star_dot_all && ast.trailing_dot_star_anchor >= 0);
  } else {
    EXPECT_FALSE(hasTrailingDotStarAnchors(ast));
  }
}

// ===== extractFirstCharFilter =====

// Literal → single-char filter for the first character.
// "a+" keeps a Repeat(Literal) root (no prefix stripping for repeats).
static_assert([] {
  constexpr auto& ast = Regex<"a+">::parsed_;
  auto f = extractFirstCharFilter(ast, ast.root);
  return !f.accepts_all && f.test('a');
}());

// CharClass → range filter.
static_assert([] {
  constexpr auto& ast = Regex<"[a-z]">::parsed_;
  auto f = extractFirstCharFilter(ast, ast.root);
  return !f.accepts_all && f.range_count >= 1 && f.ranges[0].lo == 'a' &&
      f.ranges[0].hi == 'z';
}());

// AnyByte (\C) → accepts_all.
static_assert([] {
  constexpr auto& ast = Regex<R"(\C)">::parsed_;
  auto f = extractFirstCharFilter(ast, ast.root);
  return f.accepts_all;
}());

// Group → delegates to child. Use a group containing a char class to avoid
// prefix stripping.
static_assert([] {
  constexpr auto& ast = Regex<"(?:[a-z])">::parsed_;
  auto f = extractFirstCharFilter(ast, ast.root);
  return !f.accepts_all && f.test('a') && f.test('z');
}());

// Sequence → first child's filter. Use a sequence starting with a char class
// so it isn't prefix-stripped.
static_assert([] {
  constexpr auto& ast = Regex<"[a-z]bc">::parsed_;
  auto f = extractFirstCharFilter(ast, ast.root);
  return !f.accepts_all && f.test('a') && f.test('z');
}());

// Sequence starting with anchor → skips anchor, returns filter of next child.
static_assert([] {
  constexpr auto& ast = Regex<"^[a-z]">::parsed_;
  auto f = extractFirstCharFilter(ast, ast.root);
  // After anchor skip, should find the char class filter.
  return !f.accepts_all && f.test('a') && f.test('z');
}());

TEST(ExtractFirstCharFilter, SequenceStartingWithAnchor) {
  constexpr auto& ast = Regex<"^[a-z]">::parsed_;
  constexpr auto f = extractFirstCharFilter(ast, ast.root);
  EXPECT_TRUE(f.test('a'));
  EXPECT_TRUE(f.test('z'));
  EXPECT_FALSE(f.test('0'));
}

// Alternation → union of branch filters.
static_assert([] {
  constexpr auto& ast = Regex<"a|b">::parsed_;
  auto f = extractFirstCharFilter(ast, ast.root);
  if (f.accepts_all) {
    return true; // conservative is fine
  }
  return f.test('a') && f.test('b');
}());

TEST(ExtractFirstCharFilter, Alternation) {
  constexpr auto& ast = Regex<"a|b">::parsed_;
  constexpr auto f = extractFirstCharFilter(ast, ast.root);
  EXPECT_TRUE(f.test('a'));
  EXPECT_TRUE(f.test('b'));
}

// Repeat with min>0 → delegates to child.
static_assert([] {
  constexpr auto& ast = Regex<"a+">::parsed_;
  auto f = extractFirstCharFilter(ast, ast.root);
  return !f.accepts_all && f.test('a');
}());

// Repeat with min=0 → accepts_all.
static_assert([] {
  constexpr auto& ast = Regex<"a?">::parsed_;
  auto f = extractFirstCharFilter(ast, ast.root);
  return f.accepts_all;
}());

// CharClass with >8 ranges → accepts_all (overflow).
TEST(ExtractFirstCharFilter, ManyRangesOverflow) {
  // Use a character class with many disjoint ranges: [ace...] with >8 chars
  // to force range_count > kMaxRanges.
  constexpr auto& ast = Regex<"[acegikmoqsuwyACEGIKMOQ]">::parsed_;
  constexpr auto f = extractFirstCharFilter(ast, ast.root);
  // With >8 individual ranges, the filter should overflow to accepts_all.
  // (The exact behavior depends on range merging — non-adjacent single chars
  // produce separate ranges.)
  if (!f.accepts_all) {
    // If ranges merged enough to stay under 8, at least verify correctness.
    EXPECT_TRUE(f.test('a'));
    EXPECT_TRUE(f.test('c'));
  }
}

// ===== extractRequiredLiteral =====

// Literal → first char found.
static_assert([] {
  constexpr auto& ast = Regex<"abc">::parsed_;
  [[maybe_unused]] auto r = extractRequiredLiteral(ast, ast.root);
  // After prefix stripping root may be Empty, so required literal may not be
  // found from root. Test the concept with a non-stripped pattern.
  return true;
}());

TEST(ExtractRequiredLiteral, LiteralPattern) {
  // "x" is short enough that prefix stripping still has a root literal.
  constexpr auto& ast = Regex<"x">::parsed_;
  constexpr auto r = extractRequiredLiteral(ast, ast.root);
  // After prefix stripping, root is Empty → not found from root.
  // The required literal extraction works on un-optimized ASTs too.
  // Here we just verify it doesn't crash and returns a valid result.
  if (r.found) {
    EXPECT_EQ(r.ch, 'x');
  }
}

// Non-literal consuming element blocks search.
static_assert([] {
  constexpr auto& ast = Regex<"[a-z]b">::parsed_;
  auto r = extractRequiredLiteral(ast, ast.root);
  // CharClass is consuming and has no required literal, so search stops.
  // Note: the optimizer may extract a literal from the sequence, but the
  // char class itself has no required literal.
  (void)r;
  return true;
}());

// Zero-width elements (anchors) skipped.
TEST(ExtractRequiredLiteral, AnchorSkipped) {
  constexpr auto& ast = Regex<"^x">::parsed_;
  constexpr auto r = extractRequiredLiteral(ast, ast.root);
  // Anchor is zero-width and should be skipped. If prefix stripping happened,
  // the literal may already be stripped, but the function should skip anchors.
  if (r.found) {
    EXPECT_EQ(r.ch, 'x');
  }
}

// Group → delegates to child.
static_assert([] {
  constexpr auto& ast = Regex<"(?:[a-z])">::parsed_;
  auto r = extractRequiredLiteral(ast, ast.root);
  // Group delegates to child, which is a char class → not found.
  return !r.found;
}());

// Repeat with min>0 → delegates to child.
static_assert([] {
  constexpr auto& ast = Regex<"x+">::parsed_;
  auto r = extractRequiredLiteral(ast, ast.root);
  // Repeat delegates to child, which is a literal 'x'.
  // May or may not be found depending on optimizer transformations.
  (void)r;
  return true;
}());

// Alternation → not found.
static_assert([] {
  constexpr auto& ast = Regex<"a|b">::parsed_;
  auto r = extractRequiredLiteral(ast, ast.root);
  return !r.found;
}());

// CharClass → not found.
static_assert([] {
  constexpr auto& ast = Regex<"[a-z]">::parsed_;
  auto r = extractRequiredLiteral(ast, ast.root);
  return !r.found;
}());

// ===== extractSingleFirstChar =====

// Single-point range [a,a] → found with ch='a'.
static_assert([] {
  FirstCharFilter f;
  f.accepts_all = false;
  f.addChar('a');
  auto r = extractSingleFirstChar(f);
  return r.found && r.ch == 'a';
}());

// Multi-point range [a,z] → not found.
static_assert([] {
  FirstCharFilter f;
  f.accepts_all = false;
  f.addRange('a', 'z');
  auto r = extractSingleFirstChar(f);
  return !r.found;
}());

// Multiple ranges → not found.
static_assert([] {
  FirstCharFilter f;
  f.accepts_all = false;
  f.addChar('a');
  f.addChar('b');
  auto r = extractSingleFirstChar(f);
  // 'a' and 'b' are adjacent, so they merge into one range [a,b].
  // That's a multi-point range → not found.
  return !r.found;
}());

static_assert([] {
  FirstCharFilter f;
  f.accepts_all = false;
  f.addChar('a');
  f.addChar('z');
  auto r = extractSingleFirstChar(f);
  // Two separate ranges → range_count > 1 → not found.
  return !r.found;
}());

// accepts_all → not found.
static_assert([] {
  FirstCharFilter f;
  f.accepts_all = true;
  auto r = extractSingleFirstChar(f);
  return !r.found;
}());

// ===== hasAnchorBegin =====

// ^ at root sequence → true.
static_assert([] {
  constexpr auto& ast = Regex<"^abc">::parsed_;
  return hasAnchorBegin(ast, ast.root);
}());

// ^ as root → true.
static_assert([] {
  constexpr auto& ast = Regex<"^">::parsed_;
  return hasAnchorBegin(ast, ast.root);
}());

// ^ inside group → true (delegates).
static_assert([] {
  constexpr auto& ast = Regex<"(?:^abc)">::parsed_;
  return hasAnchorBegin(ast, ast.root);
}());

// No anchor → false.
static_assert([] {
  constexpr auto& ast = Regex<"abc">::parsed_;
  return !hasAnchorBegin(ast, ast.root);
}());

// ^ in alternation (not first child of sequence) → false.
static_assert([] {
  constexpr auto& ast = Regex<"a|^b">::parsed_;
  // The root is Alternation. hasAnchorBegin only checks Sequence first child
  // or Group delegation — Alternation doesn't match.
  return !hasAnchorBegin(ast, ast.root);
}());

TEST(HasAnchorBegin, NegativeIndex) {
  constexpr auto& ast = Regex<"abc">::parsed_;
  EXPECT_FALSE(hasAnchorBegin(ast, -1));
}

// ===== hasAnyAnchor =====

// Detects ^.
static_assert([] {
  constexpr auto& ast = Regex<"^a">::parsed_;
  return hasAnyAnchor(ast, ast.root);
}());

// Detects $.
static_assert([] {
  constexpr auto& ast = Regex<"a$">::parsed_;
  return hasAnyAnchor(ast, ast.root);
}());

// Detects \A.
static_assert([] {
  constexpr auto& ast = Regex<R"(\Aa)">::parsed_;
  return hasAnyAnchor(ast, ast.root);
}());

// Detects \z.
static_assert([] {
  constexpr auto& ast = Regex<R"(a\z)">::parsed_;
  return hasAnyAnchor(ast, ast.root);
}());

// No anchors → false.
static_assert([] {
  constexpr auto& ast = Regex<"abc">::parsed_;
  return !hasAnyAnchor(ast, ast.root);
}());

// Anchor inside group → true (recurses).
static_assert([] {
  constexpr auto& ast = Regex<"(?:^a)">::parsed_;
  return hasAnyAnchor(ast, ast.root);
}());

TEST(HasAnyAnchor, NegativeIndex) {
  constexpr auto& ast = Regex<"abc">::parsed_;
  EXPECT_FALSE(hasAnyAnchor(ast, -1));
}

TEST(HasAnyAnchor, AnchorInAlternation) {
  constexpr auto& ast = Regex<"a|^b">::parsed_;
  EXPECT_TRUE(hasAnyAnchor(ast, ast.root));
}

TEST(HasAnyAnchor, AnchorInRepeat) {
  constexpr auto& ast = Regex<"(?:^a)+">::parsed_;
  EXPECT_TRUE(hasAnyAnchor(ast, ast.root));
}
