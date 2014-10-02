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

#include <folly/Benchmark.h>
#include <folly/wangle/Future.h>
#include <folly/wangle/Promise.h>

using namespace folly::wangle;

template <class T>
T incr(Try<T>&& t) {
  return t.value() + 1;
}

void someThens(size_t n) {
  auto f = makeFuture<int>(42);
  for (size_t i = 0; i < n; i++) {
    f = f.then(incr<int>);
  }
}

BENCHMARK(constantFuture) {
  makeFuture(42);
}

// This shouldn't get too far below 100%
BENCHMARK_RELATIVE(promiseAndFuture) {
  Promise<int> p;
  Future<int> f = p.getFuture();
  p.setValue(42);
  f.value();
}

// The higher the better. At the time of writing, it's only about 40% :(
BENCHMARK_RELATIVE(withThen) {
  Promise<int> p;
  Future<int> f = p.getFuture().then(incr<int>);
  p.setValue(42);
  f.value();
}

// thens
BENCHMARK_DRAW_LINE()

BENCHMARK(oneThen) {
  someThens(1);
}

// look for >= 50% relative
BENCHMARK_RELATIVE(twoThens) {
  someThens(2);
}

// look for >= 25% relative
BENCHMARK_RELATIVE(fourThens) {
  someThens(4);
}

// look for >= 1% relative
BENCHMARK_RELATIVE(hundredThens) {
  someThens(100);
}

int main() {
  folly::runBenchmarks();
}
