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
#include <folly/synchronization/Rcu.h>

using namespace folly;
using rcu_domain = folly::rcu_domain;

BENCHMARK(RcuReader, iters) {
  BenchmarkSuspender susp;

  { std::scoped_lock<rcu_domain> g(rcu_default_domain()); }
  susp.dismiss();

  // run the test loop
  while (iters--) {
    std::scoped_lock<rcu_domain> g(rcu_default_domain());
  }
}

BENCHMARK(RcuReaderNested, iters) {
  BenchmarkSuspender susp;

  { std::scoped_lock<rcu_domain> g(rcu_default_domain()); }
  susp.dismiss();

  // run the test loop
  {
    std::scoped_lock<rcu_domain> outer(rcu_default_domain());
    while (iters--) {
      std::scoped_lock<rcu_domain> inner(rcu_default_domain());
    }
  }
}

BENCHMARK(RcuRetire, iters) {
  BenchmarkSuspender susp;

  rcu_retire<int>(nullptr, [](int*) {});
  susp.dismiss();

  while (iters--) {
    rcu_retire<int>(nullptr, [](int*) {});
  }
}

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();

  return 0;
}
