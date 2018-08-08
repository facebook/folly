/*
 * Copyright 2018-present Facebook, Inc.
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

#include <folly/stats/detail/DigestBuilder-defs.h>

#include <chrono>
#include <condition_variable>
#include <thread>

#include <boost/thread/barrier.hpp>

#include <folly/Benchmark.h>
#include <folly/Range.h>
#include <folly/portability/GFlags.h>

DEFINE_int32(digest_merge_time_ns, 5500, "Time to merge into the digest");

using namespace folly;
using namespace folly::detail;

class FreeDigest {
 public:
  explicit FreeDigest(size_t) {}

  FreeDigest merge(Range<const double*>) const {
    auto start = std::chrono::steady_clock::now();
    auto finish = start + std::chrono::nanoseconds{FLAGS_digest_merge_time_ns};
    while (std::chrono::steady_clock::now() < finish) {
    }
    return FreeDigest(100);
  }
};

unsigned int append(unsigned int iters, size_t bufSize, size_t nThreads) {
  iters = 1000000;
  auto buffer = std::make_shared<DigestBuilder<FreeDigest>>(bufSize, 100);

  auto barrier = std::make_shared<boost::barrier>(nThreads + 1);

  std::vector<std::thread> threads;
  threads.reserve(nThreads);
  BENCHMARK_SUSPEND {
    for (size_t i = 0; i < nThreads; ++i) {
      threads.emplace_back([&]() {
        barrier->wait();
        for (size_t iter = 0; iter < iters; ++iter) {
          buffer->append(iter);
        }
        barrier->wait();
      });
    }
    barrier->wait();
  }
  barrier->wait();

  BENCHMARK_SUSPEND {
    for (auto& thread : threads) {
      thread.join();
    }
  }
  return iters;
}

BENCHMARK_NAMED_PARAM_MULTI(append, 1000x1, 1000, 1)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(append, 1000x2, 1000, 2)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(append, 1000x4, 1000, 4)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(append, 1000x8, 1000, 8)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(append, 1000x16, 1000, 16)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(append, 1000x32, 1000, 32)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM_MULTI(append, 10000x1, 10000, 1)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(append, 10000x2, 10000, 2)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(append, 10000x4, 10000, 4)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(append, 10000x8, 10000, 8)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(append, 10000x16, 10000, 16)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(append, 10000x32, 10000, 32)

/*
 * ./digest_buffer_benchmark
 * ============================================================================
 * folly/stats/test/DigestBuilderBenchmark.cpp     relative  time/iter  iters/s
 * ============================================================================
 * append(1000x1)                                              25.70ns   38.91M
 * append(1000x2)                                    99.12%    25.93ns   38.57M
 * append(1000x4)                                    98.62%    26.06ns   38.37M
 * append(1000x8)                                    96.23%    26.70ns   37.45M
 * append(1000x16)                                   88.75%    28.96ns   34.53M
 * append(1000x32)                                   66.81%    38.46ns   26.00M
 * ----------------------------------------------------------------------------
 * append(10000x1)                                             25.39ns   39.38M
 * append(10000x2)                                   98.81%    25.70ns   38.91M
 * append(10000x4)                                   98.76%    25.71ns   38.90M
 * append(10000x8)                                   98.95%    25.66ns   38.97M
 * append(10000x16)                                  72.90%    34.83ns   28.71M
 * append(10000x32)                                  85.15%    29.82ns   33.54M
 * ============================================================================
 */

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
