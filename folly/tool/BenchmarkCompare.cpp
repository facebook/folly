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

#include <folly/Benchmark.h>
#include <folly/FileUtil.h>
#include <folly/init/Init.h>
#include <folly/json/json.h>

/**
 * Utility to produce a relative benchmark result from JSON result dumps
 * generated earlier.
 *
 * This can be useful in cases where you are changing the code you wish to
 * benchmark, by preserving a version of the previous result you can readily
 * output the relative change by your changes.
 *
 * Usage:
 * - generate a benchmark JSON dump, use folly::benchmark's --bm_json_verbose
 *     $ your_benchmark_binary --benchmark --bm_json_verbose old-json
 * - compare two benchmarks & output a human-readable comparison:
 *     $ benchmark_compare old-json new-json
 */
namespace folly {

std::vector<detail::BenchmarkResult> resultsFromFile(
    const std::string& filename) {
  std::string content;
  readFile(filename.c_str(), content);
  std::vector<detail::BenchmarkResult> ret;
  benchmarkResultsFromDynamic(parseJson(content), ret);
  return ret;
}

void compareBenchmarkResults(const std::string& base, const std::string& test) {
  printResultComparison(resultsFromFile(base), resultsFromFile(test));
}

} // namespace folly

int main(int argc, char** argv) {
  folly::Init init(&argc, &argv);
  CHECK_GT(argc, 2);
  folly::compareBenchmarkResults(argv[1], argv[2]);
  return 0;
}
