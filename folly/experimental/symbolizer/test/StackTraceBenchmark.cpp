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

#include <folly/Benchmark.h>
#include <folly/experimental/symbolizer/StackTrace.h>
#include <folly/init/Init.h>

using namespace folly;
using namespace folly::symbolizer;

unsigned int basic(size_t nFrames) {
  unsigned int iters = 1000000;
  constexpr size_t kMaxAddresses = 100;
  uintptr_t addresses[kMaxAddresses];

  CHECK_LE(nFrames, kMaxAddresses);

  for (unsigned int i = 0; i < iters; ++i) {
    ssize_t n = getStackTrace(addresses, nFrames);
    CHECK_NE(n, -1);
  }
  // Reduce iters by nFrames as the recursive call will add it up.
  return iters - nFrames;
}

unsigned int Recurse(size_t nFrames, size_t remaining) {
  if (remaining == 0) {
    return basic(nFrames);
  } else {
    // +1 to prevent tail recursion optimization from kicking in.
    return Recurse(nFrames, remaining - 1) + 1;
  }
}

unsigned int SetupStackAndTest(int /* iters */, size_t nFrames) {
  return Recurse(nFrames, nFrames);
}

BENCHMARK_NAMED_PARAM_MULTI(SetupStackAndTest, 2_frames, 2)
BENCHMARK_NAMED_PARAM_MULTI(SetupStackAndTest, 4_frames, 4)
BENCHMARK_NAMED_PARAM_MULTI(SetupStackAndTest, 8_frames, 8)
BENCHMARK_NAMED_PARAM_MULTI(SetupStackAndTest, 16_frames, 16)
BENCHMARK_NAMED_PARAM_MULTI(SetupStackAndTest, 32_frames, 32)
BENCHMARK_NAMED_PARAM_MULTI(SetupStackAndTest, 64_frames, 64)

/**
============================================================================
                                                          time/iter  iters/s
============================================================================
SetupStackAndTest(2_frames)                                 51.68ns   19.35M
SetupStackAndTest(4_frames)                                 72.59ns   13.78M
SetupStackAndTest(8_frames)                                117.03ns    8.54M
SetupStackAndTest(16_frames)                               195.54ns    5.11M
SetupStackAndTest(32_frames)                               384.70ns    2.60M
SetupStackAndTest(64_frames)                               728.65ns    1.37M
============================================================================
*/

int main(int argc, char* argv[]) {
  folly::init(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
