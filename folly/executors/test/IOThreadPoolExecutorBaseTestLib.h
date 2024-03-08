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
#include <memory>
#include <optional>
#include <random>

#include <fmt/format.h>
#include <glog/logging.h>
#include <folly/Random.h>
#include <folly/container/F14Set.h>
#include <folly/executors/IOThreadPoolExecutor.h>
#include <folly/io/async/AsyncTimeout.h>
#include <folly/portability/GTest.h>
#include <folly/synchronization/Baton.h>

namespace folly {
namespace test {

template <typename T>
class IOThreadPoolExecutorBaseTest : public ::testing::Test {
 public:
  IOThreadPoolExecutorBaseTest() = default;
};

TYPED_TEST_SUITE_P(IOThreadPoolExecutorBaseTest);

TYPED_TEST_P(IOThreadPoolExecutorBaseTest, IOObserver) {
  struct EventBaseAccumulator : IOThreadPoolExecutorBase::IOObserver {
    void registerEventBase(EventBase& evb) override {
      // Observers should be called while the evbs are running, so this
      // operation should complete.
      evb.runInEventBaseThreadAndWait([&] { evbs.insert(&evb); });
    }
    void unregisterEventBase(EventBase& evb) override {
      // Same as registerEventBase().
      evb.runInEventBaseThreadAndWait([&] { evbs.erase(&evb); });
    }

    F14FastSet<EventBase*> evbs;
  };

  static constexpr size_t kNumThreads = 16;

  TypeParam ex{kNumThreads};
  auto observer1 = std::make_shared<EventBaseAccumulator>();
  auto observer2 = std::make_shared<EventBaseAccumulator>();
  ex.addObserver(observer1);
  ex.addObserver(observer2);
  EXPECT_EQ(observer1->evbs.size(), kNumThreads);
  EXPECT_EQ(observer2->evbs.size(), kNumThreads);
  ex.removeObserver(observer1);
  EXPECT_EQ(observer1->evbs.size(), 0);
  EXPECT_EQ(observer2->evbs.size(), kNumThreads);
  ex.join();
  EXPECT_EQ(observer1->evbs.size(), 0);
  EXPECT_EQ(observer2->evbs.size(), 0);
}

TYPED_TEST_P(IOThreadPoolExecutorBaseTest, GetEventBaseFromEvb) {
  static constexpr size_t kNumThreads = 16;
  TypeParam ex{kNumThreads};
  auto evbs = ex.getAllEventBases();
  std::shuffle(evbs.begin(), evbs.end(), std::default_random_engine{});

  for (auto ka : evbs) {
    auto* evb = ka.get();
    // If called from the EventBase thread, getEventBase() and add() stick to
    // the current EventBase.
    evb->runInEventBaseThreadAndWait([&] {
      EXPECT_EQ(evb, EventBaseManager::get()->getExistingEventBase());
      EXPECT_EQ(evb, ex.getEventBase());
      ex.add([&ex, evb] {
        EXPECT_EQ(evb, ex.getEventBase());
        EXPECT_EQ(evb, EventBaseManager::get()->getExistingEventBase());
      });
    });
  }
}

TYPED_TEST_P(IOThreadPoolExecutorBaseTest, StressTimeouts) {
  // Exercise multiplexed executors.
  static constexpr size_t kNumExecutors = 2;
  static constexpr size_t kNumThreads = 16;
  static constexpr size_t kNumTimersPerEvb = 100;
  static constexpr size_t kNumItersPerTimer = 16;
  static constexpr uint32_t kMaxTimeoutMs = 100;

  struct Timer : AsyncTimeout {
    using AsyncTimeout::AsyncTimeout;
    using Clock = std::chrono::steady_clock;

    void schedule() {
      timeout =
          std::chrono::milliseconds(folly::Random::rand32(1, kMaxTimeoutMs));
      start = Clock::now();
      scheduleTimeout(timeout);
    }

    void timeoutExpired() noexcept override {
      auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(
          Clock::now() - (start + timeout));
      EXPECT_GE(delay.count(), 0);
      sumDelay += delay;
      maxDelay = std::max(maxDelay, delay);

      if (++count < kNumItersPerTimer) {
        schedule();
      } else {
        done.post();
      }
    }

    std::chrono::milliseconds timeout;
    Clock::time_point start;

    size_t count = 0;
    std::chrono::milliseconds sumDelay{0};
    std::chrono::milliseconds maxDelay{0};
    Baton<> done;
  };

  std::vector<std::unique_ptr<TypeParam>> executors;
  std::vector<folly::Executor::KeepAlive<folly::EventBase>> evbs;
  for (size_t i = 0; i < kNumExecutors; ++i) {
    auto& ex = executors.emplace_back(std::make_unique<TypeParam>(kNumThreads));
    auto exEvbs = ex->getAllEventBases();
    evbs.insert(evbs.end(), exEvbs.begin(), exEvbs.end());
  }

  using TimerList = std::array<Timer, kNumTimersPerEvb>;
  std::vector<std::unique_ptr<TimerList>> timerLists;
  timerLists.reserve(evbs.size());
  for (const auto& evb : evbs) {
    auto& timerList = timerLists.emplace_back();
    timerList = std::make_unique<TimerList>();

    evb->runInEventBaseThread([&timerList, &evb] {
      for (auto& timer : *timerList) {
        timer.attachEventBase(evb.get());
        timer.schedule();
      }
    });
  }

  size_t count = 0;
  std::chrono::milliseconds sumDelay{0};
  std::chrono::milliseconds maxDelay{0};
  for (auto& timerList : timerLists) {
    for (auto& timer : *timerList) {
      timer.done.wait();
      count += timer.count;
      sumDelay += timer.sumDelay;
      maxDelay = std::max(maxDelay, timer.maxDelay);
    }
  }

  EXPECT_EQ(
      count,
      kNumExecutors * kNumThreads * kNumTimersPerEvb * kNumItersPerTimer);
  LOG(INFO) << fmt::format(
      "Avg delay: {:.3f}ms, max delay: {}ms",
      static_cast<double>(sumDelay.count()) / count,
      maxDelay.count());
}

REGISTER_TYPED_TEST_SUITE_P(
    IOThreadPoolExecutorBaseTest,
    IOObserver,
    GetEventBaseFromEvb,
    StressTimeouts);

} // namespace test
} // namespace folly
