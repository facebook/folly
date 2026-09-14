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

#include <folly/regex/Flags.h>

#include <folly/portability/GTest.h>

using namespace folly::regex;

// operator| combines flags correctly
static_assert(
    (Flags::Multiline | Flags::DotAll) ==
    static_cast<Flags>((1ULL << 3) | (1ULL << 4)));
static_assert((Flags::None | Flags::Multiline) == Flags::Multiline);
static_assert(
    (Flags::CaseInsensitive | Flags::Ungreedy) ==
    static_cast<Flags>((1ULL << 6) | (1ULL << 7)));

// operator& intersects flags correctly
static_assert((Flags::Multiline & Flags::Multiline) == Flags::Multiline);
static_assert((Flags::Multiline & Flags::DotAll) == Flags::None);
static_assert(
    ((Flags::Multiline | Flags::DotAll) & Flags::Multiline) ==
    Flags::Multiline);

// operator~ inverts flags
static_assert((Flags::None & ~Flags::Multiline) == Flags::None);
static_assert(
    ((Flags::Multiline | Flags::DotAll) & ~Flags::Multiline) == Flags::DotAll);

// hasFlag returns true for set flags
static_assert(hasFlag(Flags::Multiline, Flags::Multiline));
static_assert(hasFlag(Flags::DotAll, Flags::DotAll));
static_assert(hasFlag(Flags::ForceBacktracking, Flags::ForceBacktracking));

// hasFlag returns false for unset flags
static_assert(!hasFlag(Flags::None, Flags::Multiline));
static_assert(!hasFlag(Flags::Multiline, Flags::DotAll));
static_assert(!hasFlag(Flags::ForceNFA, Flags::ForceDFA));

// hasFlag with combined flags
static_assert(hasFlag(Flags::Multiline | Flags::DotAll, Flags::Multiline));
static_assert(hasFlag(Flags::Multiline | Flags::DotAll, Flags::DotAll));
static_assert(
    !hasFlag(Flags::Multiline | Flags::DotAll, Flags::CaseInsensitive));

// kCompilationFlagMask includes compilation flags
static_assert(hasFlag(kCompilationFlagMask, Flags::Multiline));
static_assert(hasFlag(kCompilationFlagMask, Flags::DotAll));
static_assert(hasFlag(kCompilationFlagMask, Flags::CaseInsensitive));
static_assert(hasFlag(kCompilationFlagMask, Flags::ForceReverseExecution));
static_assert(hasFlag(kCompilationFlagMask, Flags::Ungreedy));
static_assert(hasFlag(kCompilationFlagMask, Flags::NoAutoCapture));
static_assert(hasFlag(kCompilationFlagMask, Flags::Extended));

// kCompilationFlagMask excludes execution-only flags
static_assert(!hasFlag(kCompilationFlagMask, Flags::ForceBacktracking));
static_assert(!hasFlag(kCompilationFlagMask, Flags::ForceNFA));
static_assert(!hasFlag(kCompilationFlagMask, Flags::ForceDFA));

// compilationFlags strips execution-only flags (simple mask)
static_assert(
    compilationFlags(Flags::ForceBacktracking | Flags::Multiline) ==
    Flags::Multiline);
static_assert(
    compilationFlags(Flags::ForceNFA | Flags::DotAll) == Flags::DotAll);
static_assert(compilationFlags(Flags::ForceDFA) == Flags::None);
static_assert(
    compilationFlags(
        Flags::ForceBacktracking | Flags::ForceNFA | Flags::ForceDFA) ==
    Flags::None);

// compilationFlags preserves compilation flags
static_assert(compilationFlags(Flags::Multiline) == Flags::Multiline);
static_assert(
    compilationFlags(Flags::Multiline | Flags::DotAll) ==
    (Flags::Multiline | Flags::DotAll));
static_assert(
    compilationFlags(
        Flags::Multiline | Flags::DotAll | Flags::CaseInsensitive |
        Flags::ForceReverseExecution | Flags::Ungreedy | Flags::NoAutoCapture |
        Flags::Extended) ==
    (Flags::Multiline | Flags::DotAll | Flags::CaseInsensitive |
     Flags::ForceReverseExecution | Flags::Ungreedy | Flags::NoAutoCapture |
     Flags::Extended));

TEST(FlagsTest, OperatorOrCombinesFlags) {
  EXPECT_EQ(
      Flags::Multiline | Flags::DotAll,
      static_cast<Flags>((1ULL << 3) | (1ULL << 4)));
  EXPECT_EQ(Flags::None | Flags::Multiline, Flags::Multiline);
}

TEST(FlagsTest, OperatorAndIntersectsFlags) {
  EXPECT_EQ(Flags::Multiline & Flags::Multiline, Flags::Multiline);
  EXPECT_EQ(Flags::Multiline & Flags::DotAll, Flags::None);
  EXPECT_EQ(
      (Flags::Multiline | Flags::DotAll) & Flags::Multiline, Flags::Multiline);
}

TEST(FlagsTest, OperatorNotInvertsFlags) {
  EXPECT_EQ(Flags::None & ~Flags::Multiline, Flags::None);
  EXPECT_EQ(
      (Flags::Multiline | Flags::DotAll) & ~Flags::Multiline, Flags::DotAll);
}

TEST(FlagsTest, HasFlagSetFlags) {
  EXPECT_TRUE(hasFlag(Flags::Multiline, Flags::Multiline));
  EXPECT_TRUE(hasFlag(Flags::DotAll, Flags::DotAll));
}

TEST(FlagsTest, HasFlagUnsetFlags) {
  EXPECT_FALSE(hasFlag(Flags::None, Flags::Multiline));
  EXPECT_FALSE(hasFlag(Flags::Multiline, Flags::DotAll));
}

TEST(FlagsTest, HasFlagCombinedFlags) {
  auto combined = Flags::Multiline | Flags::DotAll;
  EXPECT_TRUE(hasFlag(combined, Flags::Multiline));
  EXPECT_TRUE(hasFlag(combined, Flags::DotAll));
  EXPECT_FALSE(hasFlag(combined, Flags::CaseInsensitive));
}

TEST(FlagsTest, CompilationFlagsStripsExecutionFlags) {
  EXPECT_EQ(
      compilationFlags(Flags::ForceBacktracking | Flags::Multiline),
      Flags::Multiline);
  EXPECT_EQ(compilationFlags(Flags::ForceNFA | Flags::DotAll), Flags::DotAll);
  EXPECT_EQ(compilationFlags(Flags::ForceDFA), Flags::None);
}

TEST(FlagsTest, CompilationFlagMaskIncludesCompilationFlags) {
  EXPECT_TRUE(hasFlag(kCompilationFlagMask, Flags::Multiline));
  EXPECT_TRUE(hasFlag(kCompilationFlagMask, Flags::DotAll));
  EXPECT_TRUE(hasFlag(kCompilationFlagMask, Flags::CaseInsensitive));
  EXPECT_TRUE(hasFlag(kCompilationFlagMask, Flags::ForceReverseExecution));
  EXPECT_TRUE(hasFlag(kCompilationFlagMask, Flags::Ungreedy));
  EXPECT_TRUE(hasFlag(kCompilationFlagMask, Flags::NoAutoCapture));
  EXPECT_TRUE(hasFlag(kCompilationFlagMask, Flags::Extended));
}

TEST(FlagsTest, CompilationFlagMaskExcludesExecutionFlags) {
  EXPECT_FALSE(hasFlag(kCompilationFlagMask, Flags::ForceBacktracking));
  EXPECT_FALSE(hasFlag(kCompilationFlagMask, Flags::ForceNFA));
  EXPECT_FALSE(hasFlag(kCompilationFlagMask, Flags::ForceDFA));
}

TEST(FlagsTest, CompilationFlagsPreservesCompilationFlags) {
  EXPECT_EQ(compilationFlags(Flags::Multiline), Flags::Multiline);
  auto allCompilation = Flags::Multiline | Flags::DotAll |
      Flags::CaseInsensitive | Flags::ForceReverseExecution | Flags::Ungreedy |
      Flags::NoAutoCapture | Flags::Extended;
  EXPECT_EQ(compilationFlags(allCompilation), allCompilation);
}

// --- Syntax flags and presets ---

// Underlying type is 64-bit.
static_assert(sizeof(Flags) == sizeof(uint64_t));

// normalizedCompilationFlags: Flags::None gets SyntaxPreset_Folly.
static_assert(
    normalizedCompilationFlags(Flags::None) == Flags::SyntaxPreset_Folly);

// normalizedCompilationFlags: behavior-only flags get SyntaxPreset_Folly
// ORed in, execution flags stripped.
static_assert(
    normalizedCompilationFlags(Flags::CaseInsensitive) ==
    (Flags::CaseInsensitive | Flags::SyntaxPreset_Folly));

// normalizedCompilationFlags: explicit syntax preset is preserved, execution
// flags stripped.
static_assert(
    normalizedCompilationFlags(Flags::SyntaxPreset_Re2) ==
    Flags::SyntaxPreset_Re2);

// normalizedCompilationFlags: a single Syntax_* bit suppresses the default.
static_assert(
    normalizedCompilationFlags(Flags::Syntax_Lookaround) ==
    Flags::Syntax_Lookaround);

// kSyntaxFlagMask equals SyntaxPreset_Folly | Syntax_InternalSerialized
// (the OR of all Syntax_* bits).
static_assert(
    kSyntaxFlagMask ==
    (Flags::SyntaxPreset_Folly | Flags::Syntax_InternalSerialized));

// kCompilationFlagMask includes all Syntax_* bits.
static_assert(hasFlag(kCompilationFlagMask, Flags::Syntax_AtomicGroup));
static_assert(hasFlag(kCompilationFlagMask, Flags::Syntax_Lookaround));
static_assert(hasFlag(kCompilationFlagMask, Flags::Syntax_NamedGroupAngle));
static_assert(hasFlag(kCompilationFlagMask, Flags::Syntax_NamedGroupPython));
static_assert(hasFlag(kCompilationFlagMask, Flags::Syntax_InlineFlags));
static_assert(hasFlag(kCompilationFlagMask, Flags::Syntax_NumericBackref));
static_assert(hasFlag(kCompilationFlagMask, Flags::Syntax_GBackref));
static_assert(hasFlag(kCompilationFlagMask, Flags::Syntax_KBackref));
static_assert(hasFlag(kCompilationFlagMask, Flags::Syntax_ShorthandClasses));
static_assert(hasFlag(kCompilationFlagMask, Flags::Syntax_WordBoundary));
static_assert(hasFlag(kCompilationFlagMask, Flags::Syntax_BufferAnchors));
static_assert(hasFlag(kCompilationFlagMask, Flags::Syntax_SingleByteEscape));
static_assert(hasFlag(kCompilationFlagMask, Flags::Syntax_XBraceHex));
static_assert(hasFlag(kCompilationFlagMask, Flags::Syntax_PosixCharClass));
static_assert(
    hasFlag(kCompilationFlagMask, Flags::Syntax_PossessiveQuantifier));
static_assert(hasFlag(kCompilationFlagMask, Flags::Syntax_LazyQuantifier));

// SyntaxPreset_Pcre equals SyntaxPreset_Folly (PCRE is a true superset).
static_assert(Flags::SyntaxPreset_Pcre == Flags::SyntaxPreset_Folly);

// SyntaxPreset_Ruby: has AtomicGroup, Lookaround, NamedGroupAngle; lacks
// NamedGroupPython, SingleByteEscape.
static_assert(hasFlag(Flags::SyntaxPreset_Ruby, Flags::Syntax_AtomicGroup));
static_assert(hasFlag(Flags::SyntaxPreset_Ruby, Flags::Syntax_Lookaround));
static_assert(hasFlag(Flags::SyntaxPreset_Ruby, Flags::Syntax_NamedGroupAngle));
static_assert(
    !hasFlag(Flags::SyntaxPreset_Ruby, Flags::Syntax_NamedGroupPython));
static_assert(
    !hasFlag(Flags::SyntaxPreset_Ruby, Flags::Syntax_SingleByteEscape));

// SyntaxPreset_Python: has Lookaround, NamedGroupPython; lacks
// NamedGroupAngle, AtomicGroup, PossessiveQuantifier, XBraceHex, GBackref.
static_assert(hasFlag(Flags::SyntaxPreset_Python, Flags::Syntax_Lookaround));
static_assert(
    hasFlag(Flags::SyntaxPreset_Python, Flags::Syntax_NamedGroupPython));
static_assert(
    !hasFlag(Flags::SyntaxPreset_Python, Flags::Syntax_NamedGroupAngle));
static_assert(!hasFlag(Flags::SyntaxPreset_Python, Flags::Syntax_AtomicGroup));
static_assert(
    !hasFlag(Flags::SyntaxPreset_Python, Flags::Syntax_PossessiveQuantifier));
static_assert(!hasFlag(Flags::SyntaxPreset_Python, Flags::Syntax_XBraceHex));
static_assert(!hasFlag(Flags::SyntaxPreset_Python, Flags::Syntax_GBackref));

// SyntaxPreset_Re2: no backrefs, no lookaround, no atomic, no possessive.
static_assert(!hasFlag(Flags::SyntaxPreset_Re2, Flags::Syntax_Lookaround));
static_assert(!hasFlag(Flags::SyntaxPreset_Re2, Flags::Syntax_AtomicGroup));
static_assert(!hasFlag(Flags::SyntaxPreset_Re2, Flags::Syntax_NumericBackref));
static_assert(!hasFlag(Flags::SyntaxPreset_Re2, Flags::Syntax_GBackref));
static_assert(!hasFlag(Flags::SyntaxPreset_Re2, Flags::Syntax_KBackref));
static_assert(
    !hasFlag(Flags::SyntaxPreset_Re2, Flags::Syntax_PossessiveQuantifier));
// RE2 keeps both named group forms, inline flags, shorthands, etc.
static_assert(hasFlag(Flags::SyntaxPreset_Re2, Flags::Syntax_NamedGroupAngle));
static_assert(hasFlag(Flags::SyntaxPreset_Re2, Flags::Syntax_NamedGroupPython));
static_assert(hasFlag(Flags::SyntaxPreset_Re2, Flags::Syntax_InlineFlags));
static_assert(hasFlag(Flags::SyntaxPreset_Re2, Flags::Syntax_ShorthandClasses));
static_assert(hasFlag(Flags::SyntaxPreset_Re2, Flags::Syntax_LazyQuantifier));

TEST(FlagsTest, NormalizedCompilationFlagsNoneGetsFolly) {
  EXPECT_EQ(normalizedCompilationFlags(Flags::None), Flags::SyntaxPreset_Folly);
}

TEST(FlagsTest, NormalizedCompilationFlagsBehaviorOnlyGetsFolly) {
  EXPECT_EQ(
      normalizedCompilationFlags(Flags::CaseInsensitive),
      Flags::CaseInsensitive | Flags::SyntaxPreset_Folly);
}

TEST(FlagsTest, NormalizedCompilationFlagsExplicitPresetUnchanged) {
  EXPECT_EQ(
      normalizedCompilationFlags(Flags::SyntaxPreset_Re2),
      Flags::SyntaxPreset_Re2);
}

TEST(FlagsTest, NormalizedCompilationFlagsSingleSyntaxBitSuppressesDefault) {
  EXPECT_EQ(
      normalizedCompilationFlags(Flags::Syntax_Lookaround),
      Flags::Syntax_Lookaround);
}
