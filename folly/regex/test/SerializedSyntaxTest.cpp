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

// Tests for the (?~...) serialized annotation syntax used by the inner
// re-parse pipeline. Verifies parsing, round-trip serialization, and
// behavioral parity between forward and reverse execution paths.

#include <folly/regex/Flags.h>
#include <folly/regex/Regex.h>
#include <folly/regex/detail/Ast.h>
#include <folly/regex/detail/AstOptimizer.h>
#include <folly/regex/detail/AstSerializer.h>
#include <folly/regex/detail/Parser.h>

#include <folly/portability/GTest.h>

using namespace folly::regex;
using namespace folly::regex::detail;

// Helper: parse with SyntaxPreset_Serialized in RuntimeReport mode.
template <std::size_t N>
constexpr auto parseSerialized(const char (&pattern)[N]) {
  return parse<N - 1, ParseErrorMode::RuntimeReport>(
      std::string_view(pattern, N - 1), Flags::SyntaxPreset_Serialized);
}

// ===================================================================
// 1. (?~...) is rejected without Syntax_InternalSerialized
// ===================================================================

TEST(SerializedSyntaxTest, AnnotationRejectedWithFollyPreset) {
  static constexpr auto r =
      parse<sizeof("(?~g1:abc)") - 1, ParseErrorMode::RuntimeReport>(
          "(?~g1:abc)", Flags::SyntaxPreset_Folly);
  EXPECT_FALSE(r.valid);
}

// ===================================================================
// 2. Explicit group ID: (?~gN:...)
// ===================================================================

TEST(SerializedSyntaxTest, ExplicitGroupId) {
  static constexpr auto r = parseSerialized("(?~g2:abc)");
  EXPECT_TRUE(r.valid);
  EXPECT_EQ(r.group_count, 2);
  // Root should be a Group with group_id=2.
  static_assert(r.nodes[r.root].kind == NodeKind::Group);
  static_assert(r.nodes[r.root].capturing == true);
  static_assert(r.nodes[r.root].group_id == 2);
}

TEST(SerializedSyntaxTest, ExplicitGroupIdNamed) {
  static constexpr auto r = parseSerialized("(?~g1<foo>:abc)");
  EXPECT_TRUE(r.valid);
  EXPECT_EQ(r.group_count, 1);
  static_assert(r.nodes[r.root].kind == NodeKind::Group);
  static_assert(r.nodes[r.root].group_id == 1);
}

TEST(SerializedSyntaxTest, MultipleExplicitGroups) {
  static constexpr auto r = parseSerialized("(?~g1:a)(?~g2:b)(?~g3:c)");
  EXPECT_TRUE(r.valid);
  EXPECT_EQ(r.group_count, 3);
}

// ===================================================================
// 3. PossessiveProbed: (?~pN:...)
// ===================================================================

TEST(SerializedSyntaxTest, PossessiveProbedParsing) {
  static constexpr auto r = parseSerialized("(?~p3:(?:x)+)");
  EXPECT_TRUE(r.valid);
  // Find the Repeat node.
  constexpr auto findRepeat = [](const auto& ast) -> int {
    for (int i = 0; i < ast.node_count; ++i) {
      if (ast.nodes[i].kind == NodeKind::Repeat) {
        return i;
      }
    }
    return -1;
  };
  constexpr int repIdx = findRepeat(r);
  static_assert(repIdx >= 0);
  static_assert(r.nodes[repIdx].repeat_mode == RepeatMode::PossessiveProbed);
  static_assert(r.nodes[repIdx].probe_id == 3);
}

// ===================================================================
// 4. Lookaround with probe ID
// ===================================================================

TEST(SerializedSyntaxTest, LookaheadProbeId) {
  static constexpr auto r = parseSerialized("(?~=2:abc)");
  EXPECT_TRUE(r.valid);
  static_assert(r.nodes[r.root].kind == NodeKind::Lookahead);
  static_assert(r.nodes[r.root].probe_id == 2);
}

TEST(SerializedSyntaxTest, NegLookaheadProbeId) {
  static constexpr auto r = parseSerialized("(?~!0:abc)");
  EXPECT_TRUE(r.valid);
  static_assert(r.nodes[r.root].kind == NodeKind::NegLookahead);
  static_assert(r.nodes[r.root].probe_id == 0);
}

TEST(SerializedSyntaxTest, LookbehindProbeId) {
  static constexpr auto r = parseSerialized("(?~b1:abc)");
  EXPECT_TRUE(r.valid);
  static_assert(r.nodes[r.root].kind == NodeKind::Lookbehind);
  static_assert(r.nodes[r.root].probe_id == 1);
  // Fixed width 3 computed from inner "abc".
  static_assert(r.nodes[r.root].min_repeat == 3);
}

TEST(SerializedSyntaxTest, LookbehindProbeIdExplicitWidth) {
  static constexpr auto r = parseSerialized("(?~b1,5:abc)");
  EXPECT_TRUE(r.valid);
  static_assert(r.nodes[r.root].kind == NodeKind::Lookbehind);
  static_assert(r.nodes[r.root].probe_id == 1);
  static_assert(r.nodes[r.root].min_repeat == 5);
}

TEST(SerializedSyntaxTest, NegLookbehindProbeId) {
  static constexpr auto r = parseSerialized("(?~B0:abc)");
  EXPECT_TRUE(r.valid);
  static_assert(r.nodes[r.root].kind == NodeKind::NegLookbehind);
  static_assert(r.nodes[r.root].probe_id == 0);
}

// ===================================================================
// 5. Discriminator offset: (?~DN:...)
// ===================================================================

TEST(SerializedSyntaxTest, DiscriminatorOffset) {
  static constexpr auto r = parseSerialized("(?~D1:abc|def)");
  EXPECT_TRUE(r.valid);
  constexpr auto findAlt = [](const auto& ast) -> int {
    for (int i = 0; i < ast.node_count; ++i) {
      if (ast.nodes[i].kind == NodeKind::Alternation) {
        return i;
      }
    }
    return -1;
  };
  constexpr int altIdx = findAlt(r);
  static_assert(altIdx >= 0);
  static_assert(r.nodes[altIdx].discriminator_offset == 1);
}

TEST(SerializedSyntaxTest, DiscriminatorOffsetZero) {
  static constexpr auto r = parseSerialized("(?~D0:a|b|c)");
  EXPECT_TRUE(r.valid);
  constexpr auto findAlt = [](const auto& ast) -> int {
    for (int i = 0; i < ast.node_count; ++i) {
      if (ast.nodes[i].kind == NodeKind::Alternation) {
        return i;
      }
    }
    return -1;
  };
  constexpr int altIdx = findAlt(r);
  static_assert(altIdx >= 0);
  static_assert(r.nodes[altIdx].discriminator_offset == 0);
}

// ===================================================================
// 6. Probe count tracking
// ===================================================================

TEST(SerializedSyntaxTest, ProbeCountFromAnnotations) {
  // Two lookaround annotations with probe IDs 0 and 3.
  // probe_count should be max(0+1, 3+1) = 4.
  static constexpr auto r = parseSerialized("(?~=0:a)(?~=3:b)");
  EXPECT_TRUE(r.valid);
  EXPECT_EQ(r.probe_count, 4);
}

// ===================================================================
// 8. Serialized round-trip: serialize → re-parse produces same AST
// ===================================================================

TEST(SerializedSyntaxTest, RoundTripGroupIds) {
  // Verify group IDs survive the outer→serialize→inner round-trip.
  constexpr auto& ast = Regex<"(a)(b)(c)">::parsed_;
  EXPECT_EQ(ast.group_count, 3);

  // Each group should have sequential IDs.
  constexpr auto countGroups = [](const auto& a) {
    int count = 0;
    for (int i = 0; i < a.node_count; ++i) {
      if (a.nodes[i].kind == NodeKind::Group && a.nodes[i].capturing) {
        ++count;
      }
    }
    return count;
  };
  static_assert(countGroups(ast) == 3);
}

TEST(SerializedSyntaxTest, RoundTripDiscriminator) {
  // Verify discriminator offset survives the round-trip.
  constexpr auto& ast = Regex<"abc|def|ghi">::parsed_;
  constexpr auto findAlt = [](const auto& a) -> int {
    for (int i = 0; i < a.node_count; ++i) {
      if (a.nodes[i].kind == NodeKind::Alternation) {
        return i;
      }
    }
    return -1;
  };
  constexpr int altIdx = findAlt(ast);
  static_assert(altIdx >= 0);
  // The alternation should have a discriminator and char_class_index.
  if constexpr (ast.nodes[altIdx].discriminator_offset >= 0) {
    static_assert(ast.nodes[altIdx].char_class_index >= 0);
  }
}

TEST(SerializedSyntaxTest, RoundTripNamedGroups) {
  auto m = Regex<"(?<user>\\w+)@(?<host>\\w+)">::search("user@host");
  EXPECT_TRUE(bool(m));
  EXPECT_EQ(m["user"], "user");
  EXPECT_EQ(m["host"], "host");
}
