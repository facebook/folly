/*
 * Copyright 2014 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "folly/Format.h"

#include <glog/logging.h>

#include "folly/sorted_vector_types.h"
#include "folly/Benchmark.h"

namespace {

using folly::sorted_vector_map;

sorted_vector_map<int, int> a;
sorted_vector_map<int, int> b;

BENCHMARK(merge_by_setting, iters) {
  while (iters--) {
    // copy to match merge benchmark
    auto a_cpy = a;
    auto b_cpy = b;
    for (const auto& kv : b_cpy) {
      a_cpy[kv.first] = kv.second;
    }
  }
}

BENCHMARK_RELATIVE(merge, iters) {
  while (iters--) {
    auto a_cpy = a;
    auto b_cpy = b;
    merge(a_cpy, b_cpy);
  }
}
}

// Benchmark results on my dev server (Intel(R) Xeon(R) CPU E5-2660 0 @ 2.20GHz)
//
// ============================================================================
// folly/test/SortedVectorBenchmark.cpp            relative  time/iter  iters/s
// ============================================================================
// merge_by_setting                                           482.01us    2.07K
// merge                                           2809.19%    17.16us   58.28K
// ============================================================================

int main(int argc, char *argv[]) {
  google::ParseCommandLineFlags(&argc, &argv, true);
  for (int i = 0; i < 1000; ++i) {
    a[2 * i] = 2 * i;
    b[2 * i + 1] = 2 * i + 1;
  }

  folly::runBenchmarks();
  return 0;
}
