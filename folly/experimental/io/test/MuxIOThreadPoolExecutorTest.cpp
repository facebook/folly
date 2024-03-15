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

#include <folly/experimental/io/Epoll.h>

#if FOLLY_HAS_EPOLL

#include <thread>

#include <folly/executors/test/IOThreadPoolExecutorBaseTestLib.h>
#include <folly/experimental/io/MuxIOThreadPoolExecutor.h>
#include <folly/portability/GTest.h>
#include <folly/synchronization/Latch.h>

namespace folly {
namespace test {

TEST(MuxIOThreadPoolExecutor, SingleEpollLoopCreateDestroy) {
  static constexpr size_t kNumThreads = 16;
  folly::MuxIOThreadPoolExecutor ex(kNumThreads);
}

TEST(MuxIOThreadPoolExecutor, SingleEpollLoopRun) {
  static constexpr size_t kNumThreads = 16;
  static constexpr size_t kNumEventBases = 64;
  static constexpr size_t kLoops = 10;
  folly::MuxIOThreadPoolExecutor::Options options;
  options.setNumEventBases(kNumEventBases);
  folly::MuxIOThreadPoolExecutor ex(kNumThreads, options);

  const auto evbs = ex.getAllEventBases();
  EXPECT_EQ(ex.numEventBases(), kNumEventBases);
  EXPECT_EQ(evbs.size(), kNumEventBases);

  const auto testEvbs = [&] {
    // Ensure that the poller gets to the waiting state.
    /* sleep override */ std::this_thread::sleep_for(
        std::chrono::milliseconds{100});

    folly::Latch latch(kNumEventBases * kLoops);
    for (size_t k = 0; k < kLoops; ++k) {
      for (auto evb : evbs) {
        evb->runInEventBaseThread([&]() { latch.count_down(); });
      }
    }
    latch.wait();
  };

  testEvbs();

  ex.setNumThreads(1);
  EXPECT_EQ(ex.numThreads(), 1);
  EXPECT_EQ(ex.numActiveThreads(), 1);
  testEvbs();

  ex.setNumThreads(kNumEventBases);
  EXPECT_EQ(ex.numThreads(), kNumEventBases);
  testEvbs();
}

TEST(MuxIOThreadPoolExecutor, SingleEpollLoopTimers) {
  static constexpr size_t kNumThreads = 16;
  static constexpr uint32_t kMilliseconds = 500;
  folly::MuxIOThreadPoolExecutor ex(kNumThreads);

  // Ensure that the poller gets to the waiting state.
  /* sleep override */ std::this_thread::sleep_for(
      std::chrono::milliseconds{100});

  folly::Latch latch(kNumThreads);
  for (auto evb : ex.getAllEventBases()) {
    evb->runInEventBaseThread([evb, &latch]() {
      evb->runAfterDelay([&latch]() { latch.count_down(); }, kMilliseconds);
    });
  }
  latch.wait();
}

TEST(MuxIOThreadPoolExecutor, InvalidSetNumThreads) {
  folly::MuxIOThreadPoolExecutor ex(16);
  EXPECT_EQ(ex.numEventBases(), 16);
  ex.setNumThreads(16); // No-op.
  EXPECT_THROW(ex.setNumThreads(0), std::invalid_argument);
  EXPECT_THROW(ex.setNumThreads(17), std::invalid_argument);

  EXPECT_THROW(folly::MuxIOThreadPoolExecutor(0), std::invalid_argument);
  folly::MuxIOThreadPoolExecutor::Options options;
  options.setNumEventBases(1);
  EXPECT_THROW(
      folly::MuxIOThreadPoolExecutor(2, options), std::invalid_argument);
}

INSTANTIATE_TYPED_TEST_SUITE_P(
    MuxIOThreadPoolExecutorTest,
    IOThreadPoolExecutorBaseTest,
    MuxIOThreadPoolExecutor);

} // namespace test
} // namespace folly
#endif
