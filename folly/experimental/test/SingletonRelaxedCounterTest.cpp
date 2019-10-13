/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <folly/experimental/SingletonRelaxedCounter.h>

#include <cstddef>
#include <thread>

#include <boost/thread/barrier.hpp>

#include <folly/ThreadLocal.h>
#include <folly/portability/GTest.h>

namespace folly {

namespace {
struct PrivateTag {};
} // namespace

using Counter = SingletonRelaxedCounter<size_t, PrivateTag>;

class SingletonRelaxedCounterTest : public testing::Test {};

TEST_F(SingletonRelaxedCounterTest, Basic) {
  EXPECT_EQ(0, Counter::count());
  Counter::add(3);
  EXPECT_EQ(3, Counter::count());
  Counter::sub(3);
  EXPECT_EQ(0, Counter::count());
}

TEST_F(SingletonRelaxedCounterTest, CompatibilityWithThreadLocal) {
  struct CounterNoOpInDtor {
    ~CounterNoOpInDtor() {
      Counter::add(0);
    }
  };
  ThreadLocal<CounterNoOpInDtor> tl;
  auto thread = std::thread([&] { //
    std::ignore = *tl;
  });
  thread.join();
  EXPECT_EQ(0, Counter::count());
}

TEST_F(SingletonRelaxedCounterTest, CompatibilityWithThreadLocalMany) {
  struct CounterNoOpInDtor {
    ~CounterNoOpInDtor() {
      Counter::add(0);
    }
  };
  ThreadLocal<CounterNoOpInDtor> tl;
  std::vector<std::thread> threads(16);
  for (auto& thread : threads) {
    thread = std::thread([&] { //
      std::ignore = *tl;
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
  EXPECT_EQ(0, Counter::count());
}

TEST_F(SingletonRelaxedCounterTest, MultithreadCorrectness) {
  static constexpr size_t const kPerThreadIncrements = 1000000;
  static constexpr size_t const kNumThreads = 24;
  static constexpr size_t const kNumCounters = 6;
  static constexpr size_t const kNumIters = 2;

  std::atomic<bool> stop{false};
  std::vector<std::thread> counters(kNumCounters);

  for (auto& counter : counters) {
    counter = std::thread([&] {
      while (!stop.load(std::memory_order_relaxed)) {
        std::this_thread::yield();
        Counter::count();
      }
    });
  }

  for (size_t j = 0; j < kNumIters; ++j) {
    std::vector<std::thread> threads(kNumThreads);

    boost::barrier barrier{kNumThreads + 1};

    for (auto& thread : threads) {
      thread = std::thread([&] {
        barrier.wait(); // A
        barrier.wait(); // B
        for (size_t i = 0; i < kPerThreadIncrements; ++i) {
          Counter::add(1);
        }
        barrier.wait(); // C
        barrier.wait(); // D
        for (size_t i = 0; i < kPerThreadIncrements; ++i) {
          Counter::sub(1);
        }
        barrier.wait(); // E
        barrier.wait(); // F
      });
    }

    barrier.wait(); // A
    EXPECT_EQ(0, Counter::count());

    barrier.wait(); // B
    barrier.wait(); // C
    EXPECT_EQ(kPerThreadIncrements * kNumThreads, Counter::count());

    barrier.wait(); // D
    barrier.wait(); // E
    EXPECT_EQ(0, Counter::count());

    barrier.wait(); // F

    for (auto& thread : threads) {
      thread.join();
    }
  }

  stop.store(true, std::memory_order_relaxed);
  for (auto& counter : counters) {
    counter.join();
  }

  Counter::count();
}

TEST_F(SingletonRelaxedCounterTest, Countable) {
  struct Object : SingletonRelaxedCountable<Object> {
    std::shared_ptr<int> contained;
  };
  using Access = SingletonRelaxedCountableAccess<Object>;

  EXPECT_EQ(0, Access::count());

  {
    Object a;
    EXPECT_EQ(1, Access::count());
  }
  EXPECT_EQ(0, Access::count());

  {
    Object a;
    Object b(a);
    EXPECT_EQ(2, Access::count());
  }
  EXPECT_EQ(0, Access::count());

  {
    Object a;
    Object b(std::move(a));
    EXPECT_EQ(2, Access::count());
  }
  EXPECT_EQ(0, Access::count());

  {
    Object a;
    Object b;
    b = a;
    EXPECT_EQ(2, Access::count());
  }
  EXPECT_EQ(0, Access::count());

  {
    Object a;
    Object b;
    b = std::move(a);
    EXPECT_EQ(2, Access::count());
  }
  EXPECT_EQ(0, Access::count());
}

} // namespace folly
