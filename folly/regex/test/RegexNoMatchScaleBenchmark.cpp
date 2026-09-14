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

// No-match and scan-all benchmarks at scale: search patterns on inputs
// from 1KB to 1MB where no match exists (worst-case full scan).

#include <cstring>
#include <string>

#include <folly/Portability.h>
#include <folly/regex/test/BenchmarkHelpers.h>

namespace {

std::string buildNoMatchInput(std::size_t targetSize) {
  const char* filler =
      "abcdefghijklmnopqrstuvwxyz ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghij "
      "the quick brown fox jumps over the lazy dog near the riverbank now ";
  std::size_t fillerLen = std::strlen(filler);
  std::string result;
  result.reserve(targetSize + 128);
  while (result.size() < targetSize) {
    result.append(filler, fillerLen);
  }
  result.resize(targetSize);
  return result;
}

FOLLY_NOINLINE const std::string& getNoMatchInput1K() {
  static auto s = buildNoMatchInput(1024);
  return s;
}
FOLLY_NOINLINE const std::string& getNoMatchInput10K() {
  static auto s = buildNoMatchInput(10240);
  return s;
}
FOLLY_NOINLINE const std::string& getNoMatchInput100K() {
  static auto s = buildNoMatchInput(102400);
  return s;
}
FOLLY_NOINLINE const std::string& getNoMatchInput1M() {
  static auto s = buildNoMatchInput(1048576);
  return s;
}

} // namespace

// ===== No-match scan: \d+ on all-alpha input (worst-case full scan) =====
SEARCH_BENCH(NoMatch_1KB, "\\d+", getNoMatchInput1K());
SEARCH_BENCH(NoMatch_10KB, "\\d+", getNoMatchInput10K());
SEARCH_BENCH(NoMatch_100KB, "\\d+", getNoMatchInput100K());
SEARCH_BENCH(NoMatch_1MB, "\\d+", getNoMatchInput1M());

// ===== Unanchored full-scan: search where first-char filter accepts all =====
// Pattern [a-z]{3}\d{3} has accepts_all=true first-char filter AND no required
// literal, so neither memchr nor bitmap skip applies. The DFA must try from
// every start position. On no-digit input this measures worst-case per-char
// scan cost.
SEARCH_BENCH(ScanAll_1KB, "[a-z]{3}\\d{3}", getNoMatchInput1K());
SEARCH_BENCH(ScanAll_10KB, "[a-z]{3}\\d{3}", getNoMatchInput10K());
SEARCH_BENCH(ScanAll_100KB, "[a-z]{3}\\d{3}", getNoMatchInput100K());
SEARCH_BENCH(ScanAll_1MB, "[a-z]{3}\\d{3}", getNoMatchInput1M());

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
