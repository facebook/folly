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
#include <folly/init/Init.h>
#include <folly/json.h>

#include <fstream>
#include <streambuf>

using namespace folly;

BENCHMARK(PerfJson2Obj, iters) {
  // We are using a small sample file (https://json.org/example.html),
  // but for true benchmarking a bigger JSON file is more interesting, like from
  // https://github.com/simdjson/simdjson/tree/master/jsonexamples or
  // https://github.com/chadaustin/Web-Benchmarks/tree/master/json/testdata
  std::ifstream fin("folly/test/jsonsamples/input.json");
  if (!fin.is_open()) {
    // TODO(cavalcanti): is there an equivalent to GTEST_SKIP()?
    return;
  }

  std::string str((std::istreambuf_iterator<char>(fin)),
                 std::istreambuf_iterator<char>());
  const size_t times = 50;
  for (size_t i = 0; i < times; ++i) {
    dynamic parsed = parseJson(str);
    if (!parsed.size())
      return;
  }
}

BENCHMARK(PerfObj2Json, iters) {
  std::ifstream fin("folly/test/jsonsamples/input.json");
  if (!fin.is_open()) {
    // TODO(cavalcanti): ditto.
    return;
  }

  std::string str((std::istreambuf_iterator<char>(fin)),
                 std::istreambuf_iterator<char>());
  std::string cp;
  dynamic parsed = parseJson(str);

  const size_t times = 50;
  for (size_t i = 0; i < times; ++i) {
    cp = toJson(parsed);
  }

}

// Benchmark results in a Macbook Pro 2015 (i7-4870HQ, 4th gen)
// ============================================================================
// folly/test/JsonBenchmark.cpp                   relative  time/iter   iters/s
// ============================================================================
// PerfJson2Obj                                              903.84us     1.11K
// PerfObj2Json                                              265.90us     3.76K

int main(int argc, char* argv[]) {
  init(&argc, &argv, true);
  runBenchmarks();
  return 0;
}
