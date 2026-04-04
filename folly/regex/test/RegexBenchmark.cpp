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

// Small-input regex benchmarks: common patterns on short strings.
// All engines compared. For scaled-input benchmarks (1KB-1MB), see
// RegexSearchScaleBenchmark, RegexNoMatchScaleBenchmark, and
// RegexMatchScaleBenchmark.

#include <string>

#include <folly/Portability.h>
#include <folly/regex/test/BenchmarkHelpers.h>

using namespace folly::regex;

namespace {

FOLLY_NOINLINE const std::string& getSimpleInput() {
  static const std::string s = "hello world";
  return s;
}
FOLLY_NOINLINE const std::string& getDigitInput() {
  static const std::string s = "abc 12345 def";
  return s;
}
FOLLY_NOINLINE const std::string& getEmailInput() {
  static const std::string s = "contact user@example.com for more information";
  return s;
}
FOLLY_NOINLINE const std::string& getVersionInput() {
  static const std::string s = "version 1.23.456 released";
  return s;
}
FOLLY_NOINLINE const std::string& getQuantifierInput() {
  static const std::string s = "hello123world";
  return s;
}
FOLLY_NOINLINE const std::string& getAlternationInput() {
  static const std::string s = "the quick brown fox jumps";
  return s;
}

} // namespace

SEARCH_BENCH(SimpleLiteral, "hello", getSimpleInput());
SEARCH_BENCH(DigitExtraction, "\\d+", getDigitInput());
SEARCH_BENCH(VersionCapture, "(\\d+)\\.(\\d+)\\.(\\d+)", getVersionInput());
SEARCH_BENCH(Alternation, "quick|slow|fast|lazy", getAlternationInput());
SEARCH_BENCH(EmailLike, "\\w+@\\w+\\.\\w+", getEmailInput());
MATCH_BENCH(QuantifierHeavy, "[a-z]+[0-9]*[a-z]+", getQuantifierInput());

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
