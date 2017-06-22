/*
 * Copyright 2017 Facebook, Inc.
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

#define HAZPTR_AMB true
#define HAZPTR_TC true

#include <folly/experimental/hazptr/bench/HazptrBench.h>
#include <folly/portability/GFlags.h>
#include <folly/portability/GTest.h>

using namespace folly::hazptr;

int nthreads;
int size;

BENCHMARK(amb, iters) {
  run_once(nthreads, size, iters);
}

BENCHMARK(amb_dup, iters) {
  run_once(nthreads, size, iters);
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  std::cout << "------------------------------------------------- AMB - TC\n";
  for (int i : nthr) {
    nthreads = i;
    for (int j : sizes) {
      size = j;
      std::cout << i << " threads -- " << j << "-item list" << std::endl;
      bench("amb - tc                    ", i, j, 0);
      bench("amb - tc - dup              ", i, j, 0);
    }
  }
  std::cout << "----------------------------------------------------------\n";
}
