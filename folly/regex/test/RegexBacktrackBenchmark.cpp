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

// Benchmark patterns that trigger catastrophic backtracking in naive
// backtracking engines. These patterns exercise Folly's backtracking budget
// protection (which falls through to NFA/DFA) and compare against other
// engines' ReDoS mitigations.
//
// std::regex and boost::regex are skipped on ReDoS patterns because they
// use recursive backtracking that either hangs (O(2^N)) or throws
// a "complexity exceeded" exception.

#include <string>

#include <folly/Portability.h>
#include <folly/regex/test/BenchmarkHelpers.h>

using namespace folly::regex;

namespace {

// FOLLY_NOINLINE prevents the compiler from seeing the string content at
// call sites, which stops constant-folding of compile-time regex engines
// through known input data.

FOLLY_NOINLINE const std::string& getA20() {
  static const std::string s(20, 'a');
  return s;
}
FOLLY_NOINLINE const std::string& getA30() {
  static const std::string s(30, 'a');
  return s;
}
FOLLY_NOINLINE const std::string& getX25() {
  static const std::string s(25, 'x');
  return s;
}
FOLLY_NOINLINE const std::string& getNoX100() {
  static const std::string s(100, 'a');
  return s;
}
FOLLY_NOINLINE const std::string& getNoX1000() {
  static const std::string s(1000, 'a');
  return s;
}
FOLLY_NOINLINE const std::string& getAlphaNum30() {
  static const std::string s = "abcdef1234567890abcdef12345678";
  return s;
}

} // namespace

// ===== Classic ReDoS patterns =====
// std::regex and boost::regex are skipped: they either hang (exponential
// backtracking) or throw complexity exceptions on these patterns.

#define REDOS_REASON "exponential backtracking in recursive engine"

// (a+)+b — nested quantifiers, O(2^N) for naive backtrackers
MATCH_BENCH_NO_RECURSIVE(NestedPlus_20, "(a+)+b", getA20(), REDOS_REASON);
MATCH_BENCH_NO_RECURSIVE(NestedPlus_30, "(a+)+b", getA30(), REDOS_REASON);

// (a*)*b — nested star, even more ambiguous than nested plus
MATCH_BENCH_NO_RECURSIVE(NestedStar_20, "(a*)*b", getA20(), REDOS_REASON);

// (x+x+)+y — two adjacent quantifiers on the same char
MATCH_BENCH_NO_RECURSIVE(DoubleQuant_25, "(x+x+)+y", getX25(), REDOS_REASON);

// (a|a)+b — ambiguous alternation, exponential splitting
MATCH_BENCH_NO_RECURSIVE(AmbigAlt_20, "(a|a)+b", getA20(), REDOS_REASON);

// .*x on no-match — greedy dot-star backtrack
MATCH_BENCH_NO_RECURSIVE(DotStar_100, ".*x", getNoX100(), REDOS_REASON);
MATCH_BENCH_NO_RECURSIVE(DotStar_1000, ".*x", getNoX1000(), REDOS_REASON);

// (\w+|\d+)+z — real-world ReDoS: overlapping char classes in alternation
// with nested quantifier
MATCH_BENCH_NO_RECURSIVE(
    QuantAlt_30, R"((\w+|\d+)+z)", getAlphaNum30(), REDOS_REASON);

#undef REDOS_REASON

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
