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

#include <iostream>
#include <folly/portability/GTest.h>
#include <folly/regex/Regex.h>
#include <folly/regex/test/CrossEngineTestHelpers.h>

using namespace folly::regex;

TEST(LookaroundProbe, InfrastructureState) {
  using R = Regex<R"(\d+(?=px))">;
  EXPECT_TRUE(R::parsed_.nfa_compatible);
  EXPECT_GT(R::parsed_.probe_count, 0) << "probe_count should be > 0";

  // Probe store should have the probe
  EXPECT_GT(R::probeStore_.probe_count, 0) << "probeStore should have probes";
  EXPECT_TRUE(R::probeStore_.hasProbe(0)) << "probe 0 should exist";
  EXPECT_GE(R::probeStore_.probes[0].root, 0) << "probe root should be valid";

  // Print probe store details
  std::cerr << "probeStore: probe_count=" << R::probeStore_.probe_count
            << " node_count=" << R::probeStore_.node_count << "\n";
  for (int i = 0; i < R::probeStore_.probe_count; ++i) {
    std::cerr << "  probe[" << i << "] root=" << R::probeStore_.probes[i].root
              << " dir=" << (int)R::probeStore_.probes[i].dir
              << " negated=" << R::probeStore_.probes[i].negated
              << " lb_width=" << R::probeStore_.probes[i].lookbehind_width
              << "\n";
  }

  // NFA should have embedded lookaround probe
  EXPECT_TRUE(R::nfaProg_.has_lookaround_probes);
  EXPECT_GT(R::nfaProg_.lookaround_probe_count, 0)
      << "NFA should have lookaround probes";
  EXPECT_GE(R::nfaProg_.lookaround_probe_start[0], 0)
      << "probe start state should be valid";

  std::cerr << "NFA: state_count=" << R::nfaProg_.state_count
            << " la_probe_count=" << R::nfaProg_.lookaround_probe_count << "\n";
  for (int i = 0; i < R::nfaProg_.lookaround_probe_count; ++i) {
    std::cerr << "  la_probe_start[" << i
              << "]=" << R::nfaProg_.lookaround_probe_start[i] << "\n";
  }

  // Print all NFA states
  for (int i = 0; i < R::nfaProg_.state_count; ++i) {
    auto& s = R::nfaProg_.states[i];
    std::cerr << "  state[" << i << "] kind=" << (int)s.kind
              << " next=" << s.next << " alt=" << s.alt
              << " probe_id=" << s.probe_id << " ch='" << s.ch << "'"
              << " cci=" << s.char_class_index << "\n";
  }
}

TEST(LookaroundProbe, ForwardBTSearch) {
  auto bt = Regex<R"(\d+(?=px))", Flags::ForceBacktracking>::search("100px");
  EXPECT_TRUE(bool(bt)) << "BT should find \\d+(?=px) in '100px'";
  if (bt) {
    EXPECT_EQ(bt[0], "100");
  }
}

TEST(LookaroundProbe, ForwardNFASearch) {
  // First check findFirst directly
  using namespace folly::regex::detail;
  using R = Regex<R"(\d+(?=px))">;
  auto pos = NfaPositionSearcher<
      R::nfaProg_, R::parsed_, Flags::ForceNFA, Direction::Forward,
      R::probeStore_>::findFirst(InputView<Direction::Forward>{"100px"});
  std::cerr << "findFirst: found=" << pos.found
            << " start=" << pos.start << " end=" << pos.end << "\n";

  auto nfa = Regex<R"(\d+(?=px))", Flags::ForceNFA>::search("100px");
  EXPECT_TRUE(bool(nfa)) << "NFA should find \\d+(?=px) in '100px'";
  if (nfa) {
    EXPECT_EQ(nfa[0], "100");
  }
}

TEST(LookaroundProbe, ForwardNFANoMatch) {
  auto nfa = Regex<R"(\d+(?=px))", Flags::ForceNFA>::search("100em");
  EXPECT_FALSE(bool(nfa)) << "NFA should NOT find \\d+(?=px) in '100em'";
}

TEST(LookaroundProbe, FixedLookbehindBT) {
  auto bt =
      Regex<R"((?<=\$)\d+)", Flags::ForceBacktracking>::search("$100");
  EXPECT_TRUE(bool(bt)) << "BT should find (?<=\\$)\\d+ in '$100'";
  if (bt) {
    EXPECT_EQ(bt[0], "100");
  }
}

TEST(LookaroundProbe, FixedLookbehindNFA) {
  auto nfa = Regex<R"((?<=\$)\d+)", Flags::ForceNFA>::search("$100");
  EXPECT_TRUE(bool(nfa)) << "NFA should find (?<=\\$)\\d+ in '$100'";
  if (nfa) {
    EXPECT_EQ(nfa[0], "100");
  }
}

TEST(LookaroundProbe, SimpleLookaheadMatch) {
  // a(?=b)b — lookahead checks b, then pattern consumes it
  auto bt = Regex<R"(a(?=b)b)", Flags::ForceBacktracking>::match("ab");
  EXPECT_TRUE(bool(bt)) << "BT match a(?=b)b on 'ab'";

  auto nfa = Regex<R"(a(?=b)b)", Flags::ForceNFA>::match("ab");
  EXPECT_TRUE(bool(nfa)) << "NFA match a(?=b)b on 'ab'";
}

TEST(LookaroundProbe, SlidingWindowRegression) {
  // Possessive pattern from SearchSlidingWindow — must not regress
  using namespace folly::regex::testing;
  expectSearchAllEngines<R"([a-z]{2,}+\d)">("abcdef1", true, "abcdef1");
}

TEST(LookaroundProbe, ReverseBTSearch) {
  auto revBt = Regex<
      R"(\d+(?=px))",
      Flags::ForceBacktracking | Flags::ForceReverseExecution>::search("100px");
  EXPECT_TRUE(bool(revBt)) << "Reverse BT should find \\d+(?=px) in '100px'";
}

TEST(LookaroundProbe, ReverseNFASearch) {
  auto revNfa = Regex<
      R"(\d+(?=px))",
      Flags::ForceNFA | Flags::ForceReverseExecution>::search("100px");
  EXPECT_TRUE(bool(revNfa)) << "Reverse NFA should find \\d+(?=px) in '100px'";
}

TEST(LookaroundProbe, ReverseDFASearch) {
  // Check if DFA is valid for this pattern
  using R = Regex<R"(\d+(?=px))", Flags::ForceReverseExecution>;
  std::cerr << "RevDFA valid: " << R::dfaProg_.valid
            << " has_la_probes: " << R::dfaProg_.has_lookaround_probes << "\n";
  if constexpr (R::dfaProg_.valid) {
    auto revDfa = Regex<
        R"(\d+(?=px))",
        Flags::ForceDFA | Flags::ForceReverseExecution>::search("100px");
    EXPECT_TRUE(bool(revDfa)) << "Reverse DFA should find \\d+(?=px) in '100px'";
  }
}

TEST(LookaroundProbe, CrossEngineLookaheadSimple) {
  using namespace folly::regex::testing;
  expectSearchAllEngines<R"(\d+(?=px))">("100px", true, "100");
  expectSearchAllEngines<R"(\d+(?=px))">("100em", false);
}

TEST(LookaroundProbe, CrossEngineLookbehindSimple) {
  using namespace folly::regex::testing;
  expectSearchAllEngines<R"((?<=\$)\d+)">("$100", true, "100");
  expectSearchAllEngines<R"((?<=\$)\d+)">("100", false);
}

TEST(LookaroundProbe, CrossEngineLookbehindMatch) {
  using namespace folly::regex::testing;
  expectSearchAllEngines<R"((?<=a)b)">("ab", true, "b");
  expectSearchAllEngines<R"((?<=a)b)">("cb", false);
}

TEST(LookaroundProbe, CrossEngineNegLookbehindMatch) {
  using namespace folly::regex::testing;
  expectSearchAllEngines<R"((?<!a)b)">("cb", true, "b");
  expectSearchAllEngines<R"((?<!a)b)">("ab", false);
}
