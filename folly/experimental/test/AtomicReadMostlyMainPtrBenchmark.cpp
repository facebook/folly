/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <folly/experimental/AtomicReadMostlyMainPtr.h>

#include <folly/Benchmark.h>
#include <folly/portability/GFlags.h>

using folly::AtomicReadMostlyMainPtr;
using folly::ReadMostlySharedPtr;

BENCHMARK(SingleThreadedLoads, n) {
  auto sharedInt = std::make_shared<int>(123);
  AtomicReadMostlyMainPtr<int> data(sharedInt);
  for (unsigned i = 0; i < n; ++i) {
    ReadMostlySharedPtr<int> ptr = data.load();
  }
}

BENCHMARK(SingleThreadedStores, n) {
  auto sharedInt = std::make_shared<int>(123);
  AtomicReadMostlyMainPtr<int> data(sharedInt);
  for (unsigned i = 0; i < n; ++i) {
    data.store(sharedInt);
    ReadMostlySharedPtr<int> ptr = data.load();
  }
}

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}

/*
 * Output:
============================================================================
AtomicReadMostlyMainPtrBenchmark.cpp            relative  time/iter  iters/s
============================================================================
SingleThreadedLoads                                         14.36ns   69.65M
SingleThreadedStores                                         5.88us  170.15K
============================================================================
 */
