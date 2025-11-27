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

#include <folly/executors/StripedEDFThreadPoolExecutor.h>

#include <algorithm>
#include <vector>

#include <folly/Random.h>
#include <folly/portability/GTest.h>
#include <folly/portability/Sched.h>
#include <folly/portability/Unistd.h>

namespace {

void pinProcessToCurrentCPU() {
  cpu_set_t set;
  CPU_ZERO(&set);
  auto cpu = sched_getcpu();
  ASSERT_GE(cpu, 0);
  CPU_SET(cpu, &set);
  auto ret = sched_setaffinity(getpid(), sizeof(set), &set);
  ASSERT_EQ(ret, 0);
}

} // namespace

// StripedEDFThreadPoolExecutor is a CPUThreadPoolExecutor, so all the thread
// pool functionality is covered by its test. We just need to verify the
// ordering by deadline, which is only guaranteed for tasks submitted from the
// same LLC, so we pin the process to a processor and verify the order in a SPSC
// scenario.
TEST(StripedEDFThreadPoolExecutor, Basic) {
  // Start with an empty pool, we'll start a single thread after all the tasks
  // have been submitted.
  folly::StripedEDFThreadPoolExecutor executor(std::pair<size_t, size_t>{0, 0});

  pinProcessToCurrentCPU();
  std::vector<uint64_t> order;
  constexpr size_t kNumTasks = 100;
  for (size_t i = 0; i < kNumTasks; ++i) {
    auto deadline = folly::Random::rand64();
    executor.add([deadline, &order] { order.push_back(deadline); }, deadline);
  }

  ASSERT_EQ(executor.numThreads(), 0);
  ASSERT_EQ(order.size(), 0);
  executor.setNumThreads(1);

  executor.join();
  EXPECT_EQ(order.size(), kNumTasks);
  EXPECT_TRUE(std::is_sorted(order.begin(), order.end()));
}

TEST(StripedEDFThreadPoolExecutor, Stop) {
  folly::StripedEDFThreadPoolExecutor executor(std::pair<size_t, size_t>{0, 0});
  executor.add([] {}, 10);
  // There are no threads, the task will be dropped.
  executor.stop();
}
