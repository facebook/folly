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
#include <folly/synchronization/detail/ThreadCachedReaders.h>
#include <folly/synchronization/test/ThreadCachedEpochBench.h>

using TCR = folly::detail::ThreadCachedReaders;

BENCHMARK(IncrementLoop, iters) {
  bm_increment_loop<TCR>(iters);
}

BENCHMARK(IncrementDecrementSameLoop, iters) {
  bm_increment_decrement_same_loop<TCR>(iters);
}

BENCHMARK(IncrementDecrementSeparateLoop, iters) {
  bm_increment_decrement_separate_loop<TCR>(iters);
}

BENCHMARK_DRAW_LINE();

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();

  return 0;
}
