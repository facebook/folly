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

#include <thread>

#include <folly/Benchmark.h>
#include <folly/ThreadLocal.h>
#include <folly/synchronization/test/Barrier.h>

using namespace folly;

namespace folly {
namespace threadlocal_detail {

template <typename Tag>
struct ThreadLocalTestHelper {
  using Meta = StaticMeta<Tag, void>;
  using TLElem = ThreadLocal<int, Tag>;
  std::vector<TLElem> elements;
};

void measureAccessAllThreads(
    int iters, uint32_t totalThreads, uint32_t tlCount, uint32_t spreadPerTL) {
  struct Tag {};

  ThreadLocalTestHelper<Tag> helper;

  std::vector<std::thread> threads;
  std::vector<std::unique_ptr<test::Barrier>> threadBarriers;
  test::Barrier allThreadsBarriers{static_cast<size_t>(totalThreads + 1)};

  BENCHMARK_SUSPEND {
    helper.elements.reserve(tlCount);
    for (uint32_t i = 0; i < tlCount; ++i) {
      helper.elements.push_back({});
    }
    for (uint32_t i = 0; i < totalThreads; ++i) {
      threadBarriers.push_back(std::make_unique<test::Barrier>(2));
      threads.push_back(std::thread([&, index = i]() {
        for (uint32_t tlIdx = 0; tlIdx < tlCount; ++tlIdx) {
          uint32_t startIndex = tlIdx % totalThreads;
          uint32_t endIndex = (tlIdx + spreadPerTL) % totalThreads;
          if (startIndex < endIndex) {
            if (index >= startIndex && index <= endIndex) {
              *helper.elements[tlIdx] = tlIdx;
            }
          } else {
            if (!(index >= endIndex && index <= startIndex)) {
              *helper.elements[tlIdx] = tlIdx;
            }
          }
        }
        allThreadsBarriers.wait();
        threadBarriers[index]->wait();
      }));
    }

    // Wait for all threads to start.
    allThreadsBarriers.wait();
  }

  [[maybe_unused]] uint64_t countFound = 0;
  [[maybe_unused]] uint64_t sum = 0;
  for (int32_t k = 0; k < iters; k++) {
    for (uint32_t i = 0; i < tlCount; ++i) {
      for (auto& elem : helper.elements[i].accessAllThreads()) {
        sum += elem;
        countFound++;
      }
    }
  }

  BENCHMARK_SUSPEND {
    while (!threads.empty()) {
      threadBarriers.back()->wait();
      threads.back().join();
      threads.pop_back();
      threadBarriers.pop_back();
    }
  }
}

} // namespace threadlocal_detail
} // namespace folly

void accessAllThreads(
    int iters, uint32_t totalThreads, uint32_t tlCount, uint32_t spreadPerTL) {
  folly::threadlocal_detail::measureAccessAllThreads(
      iters, totalThreads, tlCount, spreadPerTL);
}

BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(accessAllThreads, 100t_100c_1s, 100, 100, 1)
BENCHMARK_NAMED_PARAM(accessAllThreads, 100t_10000c_10s, 100, 10000, 10)
BENCHMARK_NAMED_PARAM(accessAllThreads, 1000t_1000c_25s, 1000, 1000, 25)
BENCHMARK_NAMED_PARAM(accessAllThreads, 2000t_20000c_50s, 2000, 20000, 50)
BENCHMARK_DRAW_LINE();

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
