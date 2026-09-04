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

// Search-at-scale benchmarks: unanchored search patterns on inputs
// from 1KB to 1MB. Tests LogDate and Email search patterns.

#include <cstring>
#include <string>

#include <folly/Portability.h>
#include <folly/regex/test/BenchmarkHelpers.h>

namespace {

// Builds a string of approximately `targetSize` bytes by repeating filler
// text, with a single match planted near the end (at ~90% offset) so the
// engine must scan most of the input.
std::string buildLogInput(std::size_t targetSize) {
  const char* filler =
      "INFO server started successfully on port 8080 and ready for requests "
      "DEBUG processing request from client connection pool manager thread "
      "WARN high memory usage detected in worker process running batch job ";
  std::size_t fillerLen = std::strlen(filler);
  std::string result;
  result.reserve(targetSize + 256);
  while (result.size() < targetSize * 9 / 10) {
    result.append(filler, fillerLen);
  }
  result.append("ERROR 2025-03-24 crash detected in main loop handler ");
  while (result.size() < targetSize) {
    result.append(filler, fillerLen);
  }
  result.resize(targetSize);
  return result;
}

std::string buildEmailInput(std::size_t targetSize) {
  const char* filler =
      "The quick brown fox jumps over the lazy dog near the river bank. "
      "Pack my box with five dozen liquor jugs for the celebration today. "
      "How vexingly quick daft zebras jump across the open field rapidly. ";
  std::size_t fillerLen = std::strlen(filler);
  std::string result;
  result.reserve(targetSize + 256);
  while (result.size() < targetSize * 9 / 10) {
    result.append(filler, fillerLen);
  }
  result.append("Please contact admin@example.com for further assistance. ");
  while (result.size() < targetSize) {
    result.append(filler, fillerLen);
  }
  result.resize(targetSize);
  return result;
}

FOLLY_NOINLINE const std::string& getLogInput1K() {
  static auto s = buildLogInput(1024);
  return s;
}
FOLLY_NOINLINE const std::string& getLogInput10K() {
  static auto s = buildLogInput(10240);
  return s;
}
FOLLY_NOINLINE const std::string& getLogInput100K() {
  static auto s = buildLogInput(102400);
  return s;
}
FOLLY_NOINLINE const std::string& getLogInput1M() {
  static auto s = buildLogInput(1048576);
  return s;
}

FOLLY_NOINLINE const std::string& getEmailInput1K() {
  static auto s = buildEmailInput(1024);
  return s;
}
FOLLY_NOINLINE const std::string& getEmailInput10K() {
  static auto s = buildEmailInput(10240);
  return s;
}
FOLLY_NOINLINE const std::string& getEmailInput100K() {
  static auto s = buildEmailInput(102400);
  return s;
}
FOLLY_NOINLINE const std::string& getEmailInput1M() {
  static auto s = buildEmailInput(1048576);
  return s;
}

} // namespace

// ===== Log date search: \d{4}-\d{2}-\d{2} =====
SEARCH_BENCH(LogDate_1KB, "\\d{4}-\\d{2}-\\d{2}", getLogInput1K());
SEARCH_BENCH(LogDate_10KB, "\\d{4}-\\d{2}-\\d{2}", getLogInput10K());
SEARCH_BENCH(LogDate_100KB, "\\d{4}-\\d{2}-\\d{2}", getLogInput100K());
SEARCH_BENCH(LogDate_1MB, "\\d{4}-\\d{2}-\\d{2}", getLogInput1M());

// ===== Email search: (\w+)@(\w+)\.(\w+) =====
SEARCH_BENCH(Email_1KB, "(\\w+)@(\\w+)\\.(\\w+)", getEmailInput1K());
SEARCH_BENCH(Email_10KB, "(\\w+)@(\\w+)\\.(\\w+)", getEmailInput10K());
SEARCH_BENCH(Email_100KB, "(\\w+)@(\\w+)\\.(\\w+)", getEmailInput100K());
SEARCH_BENCH(Email_1MB, "(\\w+)@(\\w+)\\.(\\w+)", getEmailInput1M());

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
