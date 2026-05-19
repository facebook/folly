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

// Anchored match benchmarks at scale: full-scan match patterns on inputs
// from 1KB to 1MB where the DFA processes every character.

#include <cstring>
#include <string>

#include <folly/Portability.h>
#include <folly/regex/test/BenchmarkHelpers.h>

namespace {

std::string buildFullScanInput(std::size_t targetSize) {
  const char* filler = "the quick brown fox jumps over the lazy dog, ";
  std::size_t fillerLen = std::strlen(filler);
  std::string result;
  result.reserve(targetSize + 128);
  while (result.size() < targetSize) {
    result.append(filler, fillerLen);
  }
  result.resize(targetSize);
  return result;
}

std::string buildLowercaseInput(std::size_t targetSize) {
  std::string result;
  result.reserve(targetSize);
  for (std::size_t i = 0; i < targetSize; ++i) {
    result.push_back(static_cast<char>('a' + (i % 26)));
  }
  return result;
}

FOLLY_NOINLINE const std::string& getFullScanInput1K() {
  static auto s = buildFullScanInput(1024);
  return s;
}
FOLLY_NOINLINE const std::string& getFullScanInput10K() {
  static auto s = buildFullScanInput(10240);
  return s;
}
FOLLY_NOINLINE const std::string& getFullScanInput100K() {
  static auto s = buildFullScanInput(102400);
  return s;
}
FOLLY_NOINLINE const std::string& getFullScanInput1M() {
  static auto s = buildFullScanInput(1048576);
  return s;
}

FOLLY_NOINLINE const std::string& getLowercaseInput1K() {
  static auto s = buildLowercaseInput(1024);
  return s;
}
FOLLY_NOINLINE const std::string& getLowercaseInput10K() {
  static auto s = buildLowercaseInput(10240);
  return s;
}
FOLLY_NOINLINE const std::string& getLowercaseInput100K() {
  static auto s = buildLowercaseInput(102400);
  return s;
}
FOLLY_NOINLINE const std::string& getLowercaseInput1M() {
  static auto s = buildLowercaseInput(1048576);
  return s;
}

} // namespace

// ===== Full-scan match: DFA processes every char (no memchr/filter skip) =====
// Anchored match forces the DFA to consume every character in the input
// through its transition table — isolates per-character transition cost.
// Pattern [a-z ,.]+ has ~5 equiv classes: [a-z], space, comma, period, other.
// Anchored match means the DFA runs its inner loop on every character exactly
// once — no search loop, no first-char filter, no memchr.
MATCH_BENCH(FullScan_1KB, "[a-z ,.]+", getFullScanInput1K());
MATCH_BENCH(FullScan_10KB, "[a-z ,.]+", getFullScanInput10K());
MATCH_BENCH(FullScan_100KB, "[a-z ,.]+", getFullScanInput100K());
MATCH_BENCH(FullScan_1MB, "[a-z ,.]+", getFullScanInput1M());

// ===== 2-class full-scan: range classifier bypass (2 boundaries) =====
// Pattern [a-z]+ has exactly 2 equivalence classes (lowercase vs other),
// yielding 2 boundaries.  The range classifier replaces the char_to_class[]
// table load with 2 immediate comparisons.  Anchored match on all-lowercase
// input forces every character through the DFA transition loop.
MATCH_BENCH(TwoClass_1KB, "[a-z]+", getLowercaseInput1K());
MATCH_BENCH(TwoClass_10KB, "[a-z]+", getLowercaseInput10K());
MATCH_BENCH(TwoClass_100KB, "[a-z]+", getLowercaseInput100K());
MATCH_BENCH(TwoClass_1MB, "[a-z]+", getLowercaseInput1M());

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
