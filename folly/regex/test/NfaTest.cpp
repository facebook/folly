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

// Tests for NFA construction in folly/regex/detail/Nfa.h.
// Covers AlphabetPartition, NFA state counts, interval masks,
// CountedRepeat semantics, unrollCountedRepeats, probe construction,
// and partition validity.

#include <folly/regex/Regex.h>
#include <folly/regex/detail/Ast.h>
#include <folly/regex/detail/AstOptimizer.h>
#include <folly/regex/detail/Nfa.h>
#include <folly/regex/detail/Parser.h>

#include <folly/portability/GTest.h>

using namespace folly::regex;
using namespace folly::regex::detail;

// Helper to get NFA artifacts at compile time without DFA construction.
template <FixedPattern Pat>
struct NfaInfo {
  static constexpr auto parsed_ = [] {
    AstBuilder builder;
    builder.valid = true;
    Parser parser(std::string_view{Pat.data, Pat.length()}, builder);
    builder.root = parser.parseRegex(Flags::None);
    if (builder.valid && !parser.atEnd()) {
      parser.error("Unexpected character after pattern");
    }
    if (!builder.valid) {
      const char* msg = builder.error_message;
      folly::throw_exception<regex_parse_error>(msg, builder.error_pos);
    }
    assignProbeIds(builder, builder.root);
    optimizeAst(builder, builder.root, false);
    markUnreachableNodes(builder);
    constexpr std::size_t PatLen = Pat.length();
    return compact<ParseSizes{
        (PatLen < 4 ? 8 : PatLen * 2),
        (PatLen < 4 ? 16 : PatLen * 4),
        (PatLen < 4 ? 4 : PatLen),
        0,
        PatLen,
        0,
        0}>(builder);
  }();

  static constexpr auto nfaProg_ = [] {
    if constexpr (parsed_.nfa_compatible) {
      return buildNfa<parsed_.node_count * 4 + 16>(parsed_);
    } else {
      return NfaProgram<1>{};
    }
  }();

  static constexpr int nfaStates = nfaProg_.state_count;
  static constexpr bool nfaCompatible = parsed_.nfa_compatible;
};

// ===== 1. AlphabetPartition =====

TEST(NfaAlphabetPartition, BasicBoundariesAndIntervals) {
  static_assert([] {
    AlphabetPartition part;
    part.addBoundary(50);
    part.addBoundary(100);
    part.sort();
    return part.boundary_count == 2 && part.intervalCount() == 3;
  }());
}

TEST(NfaAlphabetPartition, DeduplicatesBoundaries) {
  static_assert([] {
    AlphabetPartition part;
    part.addBoundary(42);
    part.addBoundary(42);
    return part.boundary_count == 1;
  }());
}

TEST(NfaAlphabetPartition, AddBoundaryZeroIsNoop) {
  static_assert([] {
    AlphabetPartition part;
    part.addBoundary(0);
    return part.boundary_count == 0;
  }());
}

TEST(NfaAlphabetPartition, SortProducesSortedBoundaries) {
  static_assert([] {
    AlphabetPartition part;
    part.addBoundary(200);
    part.addBoundary(50);
    part.addBoundary(100);
    part.sort();
    return part.boundaries[0] == 50 && part.boundaries[1] == 100 &&
        part.boundaries[2] == 200;
  }());
}

TEST(NfaAlphabetPartition, CharToIntervalMapping) {
  static_assert([] {
    AlphabetPartition part;
    part.addBoundary(50);
    part.addBoundary(100);
    part.sort();
    // Characters before first boundary → interval 0
    bool ok = part.charToInterval(0) == 0;
    ok = ok && part.charToInterval(49) == 0;
    // Characters at or after first boundary, before second → interval 1
    ok = ok && part.charToInterval(50) == 1;
    ok = ok && part.charToInterval(99) == 1;
    // Characters at or after second boundary → interval 2
    ok = ok && part.charToInterval(100) == 2;
    ok = ok && part.charToInterval(255) == 2;
    return ok;
  }());
}

TEST(NfaAlphabetPartition, RepresentativeValues) {
  static_assert([] {
    AlphabetPartition part;
    part.addBoundary(50);
    part.addBoundary(100);
    part.sort();
    // Interval 0 representative is 0
    bool ok = part.representative(0) == 0;
    // Interval 1 representative is boundary[0] = 50
    ok = ok && part.representative(1) == 50;
    // Interval 2 representative is boundary[1] = 100
    ok = ok && part.representative(2) == 100;
    return ok;
  }());
}

TEST(NfaAlphabetPartition, IntervalCountIsBoundaryCountPlusOne) {
  static_assert([] {
    AlphabetPartition part;
    return part.intervalCount() == 1; // 0 boundaries → 1 interval
  }());
  static_assert([] {
    AlphabetPartition part;
    part.addBoundary(10);
    part.addBoundary(20);
    part.addBoundary(30);
    return part.intervalCount() == 4; // 3 boundaries → 4 intervals
  }());
}

TEST(NfaAlphabetPartition, OverflowAtMaxBoundaries) {
  static_assert([] {
    AlphabetPartition part;
    // Add kMaxBoundaries (63) distinct values
    for (int i = 1; i <= AlphabetPartition::kMaxBoundaries; ++i) {
      part.addBoundary(static_cast<unsigned char>(i));
    }
    int countBefore = part.boundary_count;
    // Adding one more should be silently dropped
    part.addBoundary(
        static_cast<unsigned char>(AlphabetPartition::kMaxBoundaries + 1));
    return countBefore == AlphabetPartition::kMaxBoundaries &&
        part.boundary_count == AlphabetPartition::kMaxBoundaries;
  }());
}

// ===== 2. NFA State Counts =====

TEST(NfaStateCounts, SimpleLiteralSmallStateCount) {
  using Info = NfaInfo<"a">;
  static_assert(Info::nfaCompatible);
  static_assert(Info::nfaStates > 0);
  static_assert(Info::nfaStates <= 5);
}

TEST(NfaStateCounts, PlusQuantifier) {
  using Info = NfaInfo<"a+">;
  static_assert(Info::nfaCompatible);
  static_assert(Info::nfaStates > 0);
  // a+ needs: Literal, Split (loop), Split (exit), Match
  EXPECT_GT(Info::nfaStates, 2);
}

TEST(NfaStateCounts, AlternationScales) {
  using InfoAB = NfaInfo<"a|b">;
  using InfoABC = NfaInfo<"a|b|c">;
  static_assert(InfoAB::nfaCompatible);
  static_assert(InfoABC::nfaCompatible);
  // More branches → more states
  EXPECT_GE(InfoABC::nfaStates, InfoAB::nfaStates);
}

TEST(NfaStateCounts, CountedRepeatUsesCounter) {
  using Info = NfaInfo<"a{3,5}">;
  static_assert(Info::nfaCompatible);
  static_assert(Info::nfaProg_.num_counters > 0);
}

// ===== 3. Interval Mask Computation =====

TEST(NfaIntervalMask, LiteralHasSingleBitMask) {
  using Info = NfaInfo<"a">;
  static_assert(Info::nfaProg_.partition.valid);
  // Find the Literal state and verify its mask has exactly one bit set
  static_assert([] {
    const auto& prog = Info::nfaProg_;
    for (int i = 0; i < prog.state_count; ++i) {
      if (prog.states[i].kind == NfaStateKind::Literal) {
        uint64_t mask = prog.states[i].interval_mask;
        // Exactly one bit set: mask != 0 && (mask & (mask-1)) == 0
        return mask != 0 && (mask & (mask - 1)) == 0;
      }
    }
    return false;
  }());
}

TEST(NfaIntervalMask, AnyByteHasAllBitsSet) {
  using Info = NfaInfo<R"(\C)">;
  static_assert(Info::nfaCompatible);
  static_assert(Info::nfaProg_.partition.valid);
  // Find the AnyByte state and verify its mask covers all intervals
  static_assert([] {
    const auto& prog = Info::nfaProg_;
    for (int i = 0; i < prog.state_count; ++i) {
      if (prog.states[i].kind == NfaStateKind::AnyByte) {
        uint64_t mask = prog.states[i].interval_mask;
        int numIntervals = prog.partition.intervalCount();
        uint64_t expected = (numIntervals >= 64)
            ? ~uint64_t{0}
            : (uint64_t{1} << numIntervals) - 1;
        return mask == expected;
      }
    }
    return false;
  }());
}

TEST(NfaIntervalMask, NonConsumingStatesHaveZeroMask) {
  using Info = NfaInfo<"a">;
  static_assert([] {
    const auto& prog = Info::nfaProg_;
    for (int i = 0; i < prog.state_count; ++i) {
      if (prog.states[i].kind == NfaStateKind::Match ||
          prog.states[i].kind == NfaStateKind::Split) {
        if (prog.states[i].interval_mask != 0) {
          return false;
        }
      }
    }
    return true;
  }());
}

// ===== 4. CountedRepeat Semantics =====

TEST(NfaCountedRepeat, ExactRepeat) {
  using Info = NfaInfo<"a{3}">;
  static_assert(Info::nfaCompatible);
  static_assert(Info::nfaProg_.num_counters > 0);
  // Find the CountedRepeat state and verify min=3, max=3
  static_assert([] {
    const auto& prog = Info::nfaProg_;
    for (int i = 0; i < prog.state_count; ++i) {
      if (prog.states[i].kind == NfaStateKind::CountedRepeat) {
        return prog.states[i].min_repeat == 3 && prog.states[i].max_repeat == 3;
      }
    }
    return false;
  }());
}

TEST(NfaCountedRepeat, BoundedRepeat) {
  using Info = NfaInfo<"a{2,5}">;
  static_assert(Info::nfaCompatible);
  static_assert(Info::nfaProg_.num_counters > 0);
  static_assert([] {
    const auto& prog = Info::nfaProg_;
    for (int i = 0; i < prog.state_count; ++i) {
      if (prog.states[i].kind == NfaStateKind::CountedRepeat) {
        return prog.states[i].min_repeat == 2 && prog.states[i].max_repeat == 5;
      }
    }
    return false;
  }());
}

TEST(NfaCountedRepeat, UnboundedRepeat) {
  using Info = NfaInfo<"a{3,}">;
  static_assert(Info::nfaCompatible);
  static_assert(Info::nfaProg_.num_counters > 0);
  static_assert([] {
    const auto& prog = Info::nfaProg_;
    for (int i = 0; i < prog.state_count; ++i) {
      if (prog.states[i].kind == NfaStateKind::CountedRepeat) {
        return prog.states[i].min_repeat == 3 &&
            prog.states[i].max_repeat == -1;
      }
    }
    return false;
  }());
}

TEST(NfaCountedRepeat, MultipleCountedRepeats) {
  using Info = NfaInfo<"a{2}b{3}">;
  static_assert(Info::nfaCompatible);
  static_assert(Info::nfaProg_.num_counters >= 2);
}

// ===== 5. unrollCountedRepeats =====

TEST(NfaUnroll, SmallRepeatFullyUnrolled) {
  using Info = NfaInfo<"a{3}">;
  static_assert(Info::nfaCompatible);
  static constexpr auto unrolled =
      unrollCountedRepeats<Info::nfaProg_.state_count * 4 + 64>(Info::nfaProg_);
  // After unrolling a small repeat, no CountedRepeat states should remain
  static_assert(unrolled.num_counters == 0);
}

TEST(NfaUnroll, UnrolledProgramHasValidStartState) {
  using Info = NfaInfo<"a{3}">;
  static constexpr auto unrolled =
      unrollCountedRepeats<Info::nfaProg_.state_count * 4 + 64>(Info::nfaProg_);
  static_assert(unrolled.start_state >= 0);
  static_assert(unrolled.state_count > 0);
}

TEST(NfaUnroll, UnrolledPreservesPartition) {
  using Info = NfaInfo<"a{3}">;
  static constexpr auto unrolled =
      unrollCountedRepeats<Info::nfaProg_.state_count * 4 + 64>(Info::nfaProg_);
  static_assert(
      unrolled.partition.boundary_count ==
      Info::nfaProg_.partition.boundary_count);
  static_assert(unrolled.partition.valid == Info::nfaProg_.partition.valid);
}

// ===== 6. Probe Construction =====

TEST(NfaProbes, PossessiveRepeatSetsPossessiveFlag) {
  // Use Regex<> directly with ForceNFA to ensure NFA is built
  using R = Regex<"a++b", Flags::ForceNFA>;
  static_assert(R::parsed_.nfa_compatible);
  EXPECT_TRUE(R::nfaProg_.has_possessive);
}

TEST(NfaProbes, LookaheadSetsLookaroundProbes) {
  using R = Regex<R"(a(?=b))", Flags::ForceNFA>;
  static_assert(R::parsed_.nfa_compatible);
  EXPECT_TRUE(R::nfaProg_.has_lookaround_probes);
  EXPECT_GT(R::nfaProg_.lookaround_probe_count, 0);
}

TEST(NfaProbes, SimplePatternHasNoProbes) {
  using Info = NfaInfo<"abc">;
  static_assert(!Info::nfaProg_.has_possessive);
  static_assert(!Info::nfaProg_.has_lookaround_probes);
}

// ===== 7. Partition Validity =====

TEST(NfaPartition, SimplePatternPartitionValid) {
  using Info = NfaInfo<"a">;
  static_assert(Info::nfaProg_.partition.valid);
}

TEST(NfaPartition, CharClassPatternPartitionValid) {
  using Info = NfaInfo<"[a-z]+">;
  static_assert(Info::nfaProg_.partition.valid);
  // [a-z] adds 2 boundaries ('a' and 'z'+1), so intervalCount is small
  EXPECT_LE(Info::nfaProg_.partition.intervalCount(), 64);
}

TEST(NfaPartition, MultipleCharClassesPartitionValid) {
  using Info = NfaInfo<"[a-zA-Z0-9]+">;
  static_assert(Info::nfaCompatible);
  static_assert(Info::nfaProg_.partition.valid);
}
