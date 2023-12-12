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

#include <folly/experimental/io/Epoll.h> // @manual

#if FOLLY_HAS_EPOLL

#include <folly/experimental/io/MuxIOThreadPoolExecutor.h>
#include <folly/portability/GTest.h>
#include <folly/synchronization/Latch.h>

namespace folly {

TEST(MuxIOThreadPoolExecutor, SingleEpollLoopCreateDestroy) {
  static constexpr size_t kNumThreads = 16;
  static constexpr size_t kMaxEvents = 64;
  folly::MuxIOThreadPoolExecutor::Options options;
  options.setMaxEvents(kMaxEvents);
  folly::MuxIOThreadPoolExecutor ex(kNumThreads, options);
}

TEST(MuxIOThreadPoolExecutor, SingleEpollLoopRun) {
  static constexpr size_t kNumThreads = 16;
  static constexpr size_t kLoops = 10;
  static constexpr size_t kMaxEvents = 64;
  folly::MuxIOThreadPoolExecutor::Options options;
  options.setMaxEvents(kMaxEvents);
  folly::MuxIOThreadPoolExecutor ex(kNumThreads, options);
  folly::Latch latch(kNumThreads * kLoops);
  for (size_t k = 0; k < kLoops; ++k) {
    for (size_t i = 0; i < kNumThreads; ++i) {
      auto* evb = ex.getEventBase();
      evb->runInEventBaseThread([&]() { latch.count_down(); });
    }
  }
  latch.wait();
}

TEST(MuxIOThreadPoolExecutor, SingleEpollLoopTimers) {
  static constexpr size_t kNumThreads = 16;
  static constexpr uint32_t kMilliseconds = 500;
  static constexpr size_t kMaxEvents = 64;
  folly::MuxIOThreadPoolExecutor::Options options;
  options.setMaxEvents(kMaxEvents);
  folly::MuxIOThreadPoolExecutor ex(kNumThreads, options);
  folly::Latch latch(kNumThreads);
  for (size_t i = 0; i < kNumThreads; ++i) {
    auto* evb = ex.getEventBase();
    evb->runInEventBaseThread([evb, &latch]() {
      evb->runAfterDelay([&latch]() { latch.count_down(); }, kMilliseconds);
    });
  }
  latch.wait();
}

} // namespace folly
#endif
