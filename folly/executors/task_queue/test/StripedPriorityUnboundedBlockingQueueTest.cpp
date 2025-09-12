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

#include <folly/executors/task_queue/StripedPriorityUnboundedBlockingQueue.h>

#include <atomic>
#include <thread>

#include <folly/Random.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/portability/GTest.h>

namespace {

// Ensure that we test on a large number of stripes independent of the hardware
// the test runs on.
struct RandomStripe {
  static const RandomStripe& get() {
    static RandomStripe instance;
    return instance;
  }

  size_t current() const { return folly::Random::rand64(numStripes()); }
  size_t numStripes() const { return 16; }
};

} // namespace

TEST(StripedPriorityUnboundedBlockingQueue, SmokeTest) {
  const size_t kNumThreads = std::thread::hardware_concurrency();
  const size_t kNumTasksPerThread = 1024;

  folly::CPUThreadPoolExecutor consumers(
      std::make_pair(kNumThreads, kNumThreads),
      folly::StripedPriorityUnboundedBlockingQueue<
          folly::CPUThreadPoolExecutor::CPUTask,
          RandomStripe>::create(2));

  folly::CPUThreadPoolExecutor producers(
      std::make_pair(kNumThreads, kNumThreads));

  std::atomic<size_t> tasksDone{0};
  for (size_t t = 0; t < kNumThreads; ++t) {
    producers.add([&] {
      for (size_t i = 0; i < kNumTasksPerThread; ++i) {
        consumers.addWithPriority(
            [&] { ++tasksDone; }, folly::Random::rand32(2));
      }
    });
  }

  producers.join();
  consumers.join();

  EXPECT_EQ(tasksDone.load(), kNumTasksPerThread * kNumThreads);
}
