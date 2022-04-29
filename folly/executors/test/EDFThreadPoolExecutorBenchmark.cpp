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

#include <array>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

#include <folly/Benchmark.h>
#include <folly/BenchmarkUtil.h>
#include <folly/MPMCQueue.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/executors/EDFThreadPoolExecutor.h>
#include <folly/executors/SoftRealTimeExecutor.h>
#include <folly/executors/ThreadPoolExecutor.h>

using namespace folly;

// Use 19 threads because it's common to use 0.8 * numCores, and 24 is a common
// number of cores.
static constexpr size_t kNumThreads = 19;

void throughput(uint32_t n, std::unique_ptr<ThreadPoolExecutor> ex) {
  while (n--) {
    ex->add([]() {});
  }
  ex->join();
}

BENCHMARK_NAMED_PARAM(
    throughput, CPUEx, std::make_unique<CPUThreadPoolExecutor>(kNumThreads))
BENCHMARK_RELATIVE_NAMED_PARAM(
    throughput, EDFEx, std::make_unique<EDFThreadPoolExecutor>(kNumThreads))

void saturated(
    uint32_t n, std::unique_ptr<ThreadPoolExecutor> ex, size_t numTasks) {
  std::atomic<size_t> numAlive{0};
  std::atomic<bool> finish{false};

  size_t expected = 0;
  while (n--) {
    while (numAlive.load(std::memory_order_relaxed) == numTasks) {
      // spin
    }

    while (!numAlive.compare_exchange_weak(
        expected, expected + 1, std::memory_order_relaxed)) {
      // try again
    }

    ex->add([&numAlive, &finish, numTasks]() {
      size_t expectedNumTasks = numTasks;
      const size_t wanted = numTasks - 1;
      while (!numAlive.compare_exchange_weak(
          expectedNumTasks, wanted, std::memory_order_relaxed)) {
        if (finish.load(std::memory_order_acquire)) {
          break;
        }
        expectedNumTasks = numTasks;
      }
    });
  }

  finish.store(true, std::memory_order_release);

  ex->join();
}

BENCHMARK_NAMED_PARAM(
    saturated, CPUEx_1, std::make_unique<CPUThreadPoolExecutor>(kNumThreads), 1)
BENCHMARK_RELATIVE_NAMED_PARAM(
    saturated, EDFEx_1, std::make_unique<EDFThreadPoolExecutor>(kNumThreads), 1)

BENCHMARK_NAMED_PARAM(
    saturated,
    CPUEx_10,
    std::make_unique<CPUThreadPoolExecutor>(kNumThreads),
    10)
BENCHMARK_RELATIVE_NAMED_PARAM(
    saturated,
    EDFEx_10,
    std::make_unique<EDFThreadPoolExecutor>(kNumThreads),
    10)

BENCHMARK_NAMED_PARAM(
    saturated,
    CPUEx_100,
    std::make_unique<CPUThreadPoolExecutor>(kNumThreads),
    100)
BENCHMARK_RELATIVE_NAMED_PARAM(
    saturated,
    EDFEx_100,
    std::make_unique<EDFThreadPoolExecutor>(kNumThreads),
    100)

void multiThreaded(uint32_t n, std::unique_ptr<ThreadPoolExecutor> ex) {
  static constexpr size_t kMallocSize = 128;
  static constexpr size_t kNumTasks = 8;

  struct WorkItem {
    std::array<std::atomic<char*>, kNumTasks> ptrs{};
    std::atomic<int> tasksDone{0};
  };

  std::array<WorkItem, 1024> workItems{};
  folly::MPMCQueue<WorkItem*> mallocQueue{workItems.size()};
  folly::MPMCQueue<WorkItem*> memsetQueue{workItems.size()};
  folly::MPMCQueue<WorkItem*> freeQueue{workItems.size()};

  for (size_t i = 0; i < std::min(static_cast<size_t>(n), workItems.size());
       i++) {
    mallocQueue.write(&workItems[i]);
  }

  auto maybeFinishState = [](WorkItem* workItem,
                             folly::MPMCQueue<WorkItem*>& nextQueue) {
    int tasksDone = workItem->tasksDone.fetch_add(1);
    if (tasksDone + 1 == kNumTasks) {
      workItem->tasksDone.store(0);
      nextQueue.write(workItem);
    }
  };

  auto mallocThread = std::thread([&]() {
    for (uint32_t i = 0; i < n; i++) {
      WorkItem* workItem;
      mallocQueue.blockingRead(workItem);
      for (size_t taskIndex = 0; taskIndex < kNumTasks; taskIndex++) {
        ex->add([workItem, taskIndex, &maybeFinishState, &memsetQueue]() {
          workItem->ptrs[taskIndex].store(
              static_cast<char*>(malloc(kMallocSize)));
          maybeFinishState(workItem, /*nextQueue=*/memsetQueue);
        });
      }
    }
  });

  auto memsetThread = std::thread([&]() {
    for (uint32_t i = 0; i < n; i++) {
      WorkItem* workItem;
      memsetQueue.blockingRead(workItem);
      for (size_t taskIndex = 0; taskIndex < kNumTasks; taskIndex++) {
        int ch = static_cast<int>(i);
        ex->add([workItem, taskIndex, ch, &maybeFinishState, &freeQueue]() {
          memset(workItem->ptrs[taskIndex].load(), ch, kMallocSize);
          maybeFinishState(workItem, /*nextQueue=*/freeQueue);
        });
      }
    }
  });

  auto freeThread = std::thread([&]() {
    for (uint32_t i = 0; i < n; i++) {
      WorkItem* workItem;
      freeQueue.blockingRead(workItem);
      for (size_t taskIndex = 0; taskIndex < kNumTasks; taskIndex++) {
        ex->add([workItem, taskIndex, &maybeFinishState, &mallocQueue]() {
          free(workItem->ptrs[taskIndex].load());
          workItem->ptrs[taskIndex].store(nullptr);
          maybeFinishState(workItem, /*nextQueue=*/mallocQueue);
        });
      }
    }
  });

  mallocThread.join();
  memsetThread.join();
  freeThread.join();
  ex->join();

  for (auto& workItem : workItems) {
    for (size_t i = 0; i < kNumTasks; i++) {
      free(workItem.ptrs[i]);
    }
  }
}

BENCHMARK_NAMED_PARAM(
    multiThreaded, CPUEx, std::make_unique<CPUThreadPoolExecutor>(kNumThreads))
BENCHMARK_RELATIVE_NAMED_PARAM(
    multiThreaded, EDFEx, std::make_unique<EDFThreadPoolExecutor>(kNumThreads))

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();

  return 0;
}
