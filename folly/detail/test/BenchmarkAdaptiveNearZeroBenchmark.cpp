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

#ifdef _MSC_VER
#include <intrin.h>
#endif

BENCHMARK(near_zero_cost, iters) {
  while (iters--) {
    // Match the global benchmark baseline so adaptive mode exercises the
    // baseline-subtracted near-zero case directly.
#ifdef _MSC_VER
    _ReadWriteBarrier();
#else
    asm volatile("");
#endif
  }
}

int main(int argc, char* argv[]) {
  folly::gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
