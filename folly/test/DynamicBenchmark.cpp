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

#include <folly/dynamic.h>

#include <folly/Benchmark.h>
#include <folly/init/Init.h>

using folly::dynamic;

BENCHMARK(moveBool, iters) {
  for (size_t i = 0; i < iters; ++i) {
    dynamic b = true;
    folly::makeUnpredictable(b);
    dynamic other = std::move(b);
    folly::doNotOptimizeAway(other);
  }
}

BENCHMARK(moveShortString, iters) {
  for (size_t i = 0; i < iters; ++i) {
    dynamic s = "short";
    folly::makeUnpredictable(s);
    dynamic other = std::move(s);
    folly::doNotOptimizeAway(other);
  }
}

BENCHMARK(moveLongString, iters) {
  for (size_t i = 0; i < iters; ++i) {
    folly::BenchmarkSuspender braces;
    dynamic s =
        "a very long string which will certainly have to go on the heap";
    folly::makeUnpredictable(s);
    braces.dismiss();
    dynamic other = std::move(s);
    folly::doNotOptimizeAway(other);
  }
}

BENCHMARK(copyBool, iters) {
  dynamic b = true;
  folly::makeUnpredictable(b);
  for (size_t i = 0; i < iters; ++i) {
    dynamic other = b;
    folly::doNotOptimizeAway(other);
  }
}

BENCHMARK(assignRawBool, iters) {
  bool b = true;
  dynamic other = false;
  folly::makeUnpredictable(b);
  for (size_t i = 0; i < iters; ++i) {
    other = b;
    folly::doNotOptimizeAway(other);
  }
}

BENCHMARK(copyShortString, iters) {
  dynamic s = "short";
  folly::makeUnpredictable(s);
  for (size_t i = 0; i < iters; ++i) {
    dynamic other = s;
    folly::doNotOptimizeAway(other);
  }
}

BENCHMARK(copyLongString, iters) {
  folly::BenchmarkSuspender braces;
  dynamic s = "a very long string which will certainly have to go on the heap";
  folly::makeUnpredictable(s);
  braces.dismiss();
  for (size_t i = 0; i < iters; ++i) {
    dynamic other = s;
    folly::doNotOptimizeAway(other);
  }
}

BENCHMARK(hashBool, iters) {
  dynamic b = true;
  folly::makeUnpredictable(b);
  for (size_t i = 0; i < iters; ++i) {
    auto hash = b.hash();
    folly::doNotOptimizeAway(hash);
  }
}

BENCHMARK(hashShortString, iters) {
  dynamic s = "short";
  folly::makeUnpredictable(s);
  for (size_t i = 0; i < iters; ++i) {
    auto hash = s.hash();
    folly::doNotOptimizeAway(hash);
  }
}

BENCHMARK(hashLongString, iters) {
  folly::BenchmarkSuspender braces;
  dynamic s = "a very long string which will certainly have to go on the heap";
  folly::makeUnpredictable(s);
  braces.dismiss();
  for (size_t i = 0; i < iters; ++i) {
    auto hash = s.hash();
    folly::doNotOptimizeAway(hash);
  }
}

BENCHMARK(sizeShortString, iters) {
  dynamic s = "short";
  folly::makeUnpredictable(s);
  for (size_t i = 0; i < iters; ++i) {
    auto sz = s.size();
    folly::doNotOptimizeAway(sz);
  }
}

BENCHMARK(sizeLongString, iters) {
  folly::BenchmarkSuspender braces;
  dynamic s = "a very long string which will certainly have to go on the heap";
  folly::makeUnpredictable(s);
  braces.dismiss();
  for (size_t i = 0; i < iters; ++i) {
    auto sz = s.size();
    folly::doNotOptimizeAway(sz);
  }
}

int main(int argc, char** argv) {
  folly::Init init(&argc, &argv);
  folly::runBenchmarks();
  return 0;
}
