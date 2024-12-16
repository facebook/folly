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
#include <folly/observer/Observer.h>

void addDependenciesBenchmark(int numDependencies, size_t iters) {
  for (size_t iteration = 0; iteration < iters; ++iteration) {
    folly::BenchmarkSuspender suspender;
    auto rootObserver = folly::observer::makeObserver([] { return 0; });
    std::vector<folly::observer::Observer<int>> dependencies;
    dependencies.reserve(numDependencies);
    suspender.dismiss();
    for (int d = 0; d < numDependencies; ++d) {
      dependencies.push_back(folly::observer::makeObserver([d, rootObserver] {
        return **rootObserver + d;
      }));
    }
    // exclude dependency destruction from benchmark
    suspender.rehire();
  }
}

void removeDependenciesBenchmark(int numDependencies, size_t iters) {
  for (size_t iteration = 0; iteration < iters; ++iteration) {
    // exclude addition from benchmark
    folly::BenchmarkSuspender suspender;
    auto rootObserver = folly::observer::makeObserver([] { return 0; });
    std::vector<folly::observer::Observer<int>> dependencies;
    dependencies.reserve(numDependencies);
    for (int d = 0; d < numDependencies; ++d) {
      dependencies.push_back(folly::observer::makeObserver([d, rootObserver] {
        return **rootObserver + d;
      }));
    }
    // Measure dependency removal
    suspender.dismiss();
    dependencies.clear();
  }
}

BENCHMARK(observer_adding_dependencies_10k, n) {
  addDependenciesBenchmark(10'000, n);
}

BENCHMARK(observer_removing_dependencies_10k, n) {
  removeDependenciesBenchmark(10'000, n);
}

BENCHMARK(observer_adding_dependencies_100k, n) {
  addDependenciesBenchmark(100'000, n);
}

BENCHMARK(observer_removing_dependencies_100k, n) {
  removeDependenciesBenchmark(100'000, n);
}

int main(int argc, char** argv) {
  folly::Init init(&argc, &argv);
  folly::runBenchmarks();
  return 0;
}
