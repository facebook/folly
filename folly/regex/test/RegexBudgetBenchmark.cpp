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

// Benchmark patterns that exercise Folly's backtracking budget mechanism.
//
// The Auto engine uses backtracking with UseBudget=true for patterns that
// are not backtrack_safe (nested quantifiers over groups/sequences).
// When the budget is exhausted, it falls through to DFA.
// ForceBacktracking disables the budget, exposing the raw cost.
//
// Pattern (ab|a)+c on "aaa...c": the optimizer rewrites this to (ab?)+c
// via prefix factoring, eliminating exponential ambiguity at compile time.
// The benchmarks demonstrate that Folly's AST optimizer prevents ReDoS
// before execution even begins.

#include <string>

#include <folly/Portability.h>
#include <folly/regex/test/BenchmarkHelpers.h>

using namespace folly::regex;

namespace {

// FOLLY_NOINLINE prevents the compiler from seeing the string content at
// call sites, which stops constant-folding of compile-time regex engines
// through known input data.

FOLLY_NOINLINE const std::string& getAmb20() {
  static const std::string s = std::string(20, 'a') + "c";
  return s;
}
FOLLY_NOINLINE const std::string& getAmb25() {
  static const std::string s = std::string(25, 'a') + "c";
  return s;
}
FOLLY_NOINLINE const std::string& getAmb30() {
  static const std::string s = std::string(30, 'a') + "c";
  return s;
}
FOLLY_NOINLINE const std::string& getAmb35() {
  static const std::string s = std::string(35, 'a') + "c";
  return s;
}
FOLLY_NOINLINE const std::string& getAmb40() {
  static const std::string s = std::string(40, 'a') + "c";
  return s;
}

} // namespace

MATCH_BENCH(AmbigLen_20, "(ab|a)+c", getAmb20());
MATCH_BENCH(AmbigLen_25, "(ab|a)+c", getAmb25());
MATCH_BENCH(AmbigLen_30, "(ab|a)+c", getAmb30());
MATCH_BENCH(AmbigLen_35, "(ab|a)+c", getAmb35());
MATCH_BENCH(AmbigLen_40, "(ab|a)+c", getAmb40());

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
