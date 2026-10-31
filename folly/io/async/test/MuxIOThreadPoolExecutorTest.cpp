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

#include <folly/io/async/Epoll.h>

#if FOLLY_HAS_EPOLL

#include <memory>
#include <thread>
#include <vector>

#include <folly/Function.h>
#include <folly/executors/test/IOThreadPoolExecutorBaseTestLib.h>
#include <folly/io/async/MuxIOThreadPoolExecutor.h>
#include <folly/portability/GTest.h>
#include <folly/synchronization/Baton.h>
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

  // Downsizing is not supported; exercise upsizing from the many-EventBases,
  // few-threads steady state.
  ex.setNumThreads(kNumEventBases);
  EXPECT_EQ(ex.numThreads(), kNumEventBases);
  testEvbs();
}

TEST(MuxIOThreadPoolExecutor, PollerRingMigration) {
  // Few threads and many EventBases with short timers, so polls armed on one
  // thread are routinely completed on another.
  static constexpr size_t kNumThreads = 2;
  static constexpr size_t kNumEventBases = 64;
  static constexpr size_t kIterationsPerEvb = 20;

  folly::MuxIOThreadPoolExecutor::Options options;
  options.setNumEventBases(kNumEventBases);
  folly::MuxIOThreadPoolExecutor ex(kNumThreads, options);

  const auto evbs = ex.getAllEventBases();
  folly::Latch latch(kNumEventBases * kIterationsPerEvb);
  std::vector<size_t> remaining(kNumEventBases, kIterationsPerEvb);

  // Each EventBase's callbacks run serially on its own loop, so remaining[i]
  // needs no extra synchronization. count_down() is the last shared access, so
  // no worker touches these locals once latch.wait() returns.
  folly::Function<void(size_t)> tick = [&](size_t i) {
    if (--remaining[i] > 0) {
      evbs[i]->runAfterDelay([&tick, i] { tick(i); }, /* milliseconds */ 1);
    }
    latch.count_down();
  };
  for (size_t i = 0; i < kNumEventBases; ++i) {
    evbs[i]->runInEventBaseThread([&tick, i] { tick(i); });
  }
  latch.wait();
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

TEST(MuxIOThreadPoolExecutor, EventBaseKeepAlivePreservesSelection) {
  constexpr size_t kNumEventBases = 3;
  MuxIOThreadPoolExecutor::Options options;
  options.setNumEventBases(kNumEventBases);
  MuxIOThreadPoolExecutor ex(2, options);
  IOExecutor& io = ex;
  const auto evbs = ex.getAllEventBases();
  ASSERT_EQ(evbs.size(), kNumEventBases);

  for (size_t i = 0; i < 2 * kNumEventBases; ++i) {
    const auto evb = io.getEventBaseKeepAlive();
    EXPECT_EQ(evb.get(), evbs[i % kNumEventBases].get());
  }

  Latch callbacks(kNumEventBases);
  for (const auto& evb : evbs) {
    evb->runInEventBaseThread([&, expected = evb.get()] {
      const auto current = io.getEventBaseKeepAlive();
      EXPECT_EQ(current.get(), expected);
      EXPECT_TRUE(current->isInEventBaseThread());
      callbacks.count_down();
    });
  }
  callbacks.wait();
  EXPECT_EQ(io.getEventBaseKeepAlive().get(), evbs.front().get());
}

TEST(
    MuxIOThreadPoolExecutor,
    EventBaseKeepAliveBlocksShutdownWithoutLockingDrain) {
  class ShutdownObserver : public IOThreadPoolExecutorBase::IOObserver {
   public:
    explicit ShutdownObserver(MuxIOThreadPoolExecutor& executor)
        : executor_(executor) {}

    void unregisterEventBase(EventBase&) noexcept override {
      EXPECT_THROW(executor_.getEventBaseKeepAlive(), std::runtime_error);
      EXPECT_THROW(executor_.getAllEventBases(), std::runtime_error);
      started.post();
    }

    Baton<> started;

   private:
    MuxIOThreadPoolExecutor& executor_;
  };

  MuxIOThreadPoolExecutor ex(1);
  auto observer = std::make_shared<ShutdownObserver>(ex);
  ex.addObserver(observer);
  auto evb = ex.getEventBaseKeepAlive();
  Baton<> stopped;
  std::thread shutdown([&] {
    ex.join();
    stopped.post();
  });
  observer->started.wait();

  EXPECT_FALSE(stopped.try_wait());
  EXPECT_THROW(ex.getEventBaseKeepAlive(), std::runtime_error);
  EXPECT_THROW(ex.getAllEventBases(), std::runtime_error);
  Baton<> callback;
  evb->runInEventBaseThread([&] {
    EXPECT_TRUE(evb->isInEventBaseThread());
    EXPECT_THROW(ex.getEventBaseKeepAlive(), std::runtime_error);
    EXPECT_THROW(ex.getAllEventBases(), std::runtime_error);
    callback.post();
  });
  callback.wait();
  EXPECT_FALSE(stopped.try_wait());

  evb.reset();
  shutdown.join();
  EXPECT_TRUE(stopped.try_wait());
  EXPECT_THROW(ex.getEventBaseKeepAlive(), std::runtime_error);
  EXPECT_THROW(ex.getAllEventBases(), std::runtime_error);
}

TEST(MuxIOThreadPoolExecutor, EventBaseKeepAliveRacesShutdown) {
  MuxIOThreadPoolExecutor ex(1);
  auto held = ex.getEventBaseKeepAlive();
  Baton<> acquired;
  size_t acquisitions = 0;
  std::thread acquire([&] {
    try {
      while (true) {
        const auto evb = ex.getEventBaseKeepAlive();
        EXPECT_EQ(evb.get(), held.get());
        const auto all = ex.getAllEventBases();
        EXPECT_EQ(all.size(), 1);
        if (++acquisitions == 1) {
          acquired.post();
        }
      }
    } catch (const std::runtime_error&) {
      EXPECT_THROW(ex.getEventBaseKeepAlive(), std::runtime_error);
    }
  });
  acquired.wait();
  Baton<> stopped;
  std::thread shutdown([&] {
    ex.join();
    stopped.post();
  });

  acquire.join();
  EXPECT_GE(acquisitions, 1);
  EXPECT_FALSE(stopped.try_wait());
  held.reset();
  shutdown.join();
  EXPECT_TRUE(stopped.try_wait());
}

TEST(MuxIOThreadPoolExecutor, InvalidSetNumThreads) {
  folly::MuxIOThreadPoolExecutor ex(16);
  EXPECT_EQ(ex.numEventBases(), 16);
  ex.setNumThreads(16); // No-op.
  EXPECT_THROW(ex.setNumThreads(0), std::invalid_argument);
  EXPECT_THROW(ex.setNumThreads(17), std::invalid_argument);
  EXPECT_THROW(ex.setNumThreads(8), std::invalid_argument); // No downsizing.

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
