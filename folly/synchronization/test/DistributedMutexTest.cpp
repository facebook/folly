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

#include <folly/synchronization/DistributedMutex.h>
#include <folly/MapUtil.h>
#include <folly/Synchronized.h>
#include <folly/container/Array.h>
#include <folly/container/Foreach.h>
#include <folly/portability/GTest.h>
#include <folly/synchronization/Baton.h>
#include <folly/test/DeterministicSchedule.h>

#include <chrono>
#include <cmath>
#include <thread>

using namespace std::literals;

namespace folly {
namespace test {
/**
 * Like DeterministicSchedule, but allows setting callbacks that can be run
 * for the current thread when an atomic access occurs, and after.  This
 * allows us to construct thread interleavings by hand
 *
 * Constructing a ManualSchedule is required to ensure that we maintain
 * per-test state for threads
 *
 * This can also be used to order thread movement, as an alternative to
 * maintaining condition variables and/or semaphores for the purposes of
 * testing, for example
 *
 *    auto one = std::thread{[&]() {
 *      schedule.wait(1);
 *      two();
 *      schedule.post(2);
 *    }};
 *
 *    auto two = std::thread{[&]() {
 *      one();
 *      schedule.post(1);
 *      schedule.wait(2);
 *      three();
 *    }};
 *
 * The code above is guaranteed to call one(), then two(), and then three()
 */
class ManualSchedule {
 public:
  ManualSchedule() = default;
  ~ManualSchedule() {
    // delete this schedule from the global map
    auto schedules = schedules_.wlock();
    for_each(*schedules, [&](auto& schedule, auto, auto iter) {
      if (schedule.second == this) {
        schedules->erase(iter);
      }
    });
  }

  /**
   * These will be invoked by DeterministicAtomic to signal atomic access
   * before and after the operation
   */
  static void beforeSharedAccess() {
    if (folly::kIsDebug) {
      auto id = std::this_thread::get_id();

      // get the schedule assigned for the current thread, if one exists,
      // otherwise proceed as normal
      auto schedule = get_ptr(*schedules_.wlock(), id);
      if (!schedule) {
        return;
      }

      // now try and get the callbacks for this thread, if there is a callback
      // registered for the test, it must mean that we have a callback
      auto callback = get_ptr((*(*schedule)->callbacks_.wlock()), id);
      if (!callback) {
        return;
      }
      (*callback)();
    }
  }
  static void afterSharedAccess(bool) {
    beforeSharedAccess();
  }

  /**
   * Set a callback that will be called on every subsequent atomic access.
   * This will be invoked before and after every atomic access, for the thread
   * that called setCallback
   */
  void setCallback(std::function<void()> callback) {
    schedules_.wlock()->insert({std::this_thread::get_id(), this});
    callbacks_.wlock()->insert({std::this_thread::get_id(), callback});
  }

  /**
   * Delete the callback set for this thread on atomic accesses
   */
  void removeCallbacks() {
    callbacks_.wlock()->erase(std::this_thread::get_id());
  }

  /**
   * wait() and post() for easy testing
   */
  void wait(int id) {
    if (folly::kIsDebug) {
      auto& baton = (*batons_.wlock())[id];
      baton.wait();
    }
  }
  void post(int id) {
    if (folly::kIsDebug) {
      auto& baton = (*batons_.wlock())[id];
      baton.post();
    }
  }

 private:
  // the map of threads to the schedule started for that test
  static Synchronized<std::unordered_map<std::thread::id, ManualSchedule*>>
      schedules_;
  // the map of callbacks to be executed for a thread's atomic accesses
  Synchronized<std::unordered_map<std::thread::id, std::function<void()>>>
      callbacks_;
  // batons for testing, this map will only ever be written to, so it is safe
  // to hold references outside lock
  Synchronized<std::unordered_map<int, folly::Baton<>>> batons_;
};

Synchronized<std::unordered_map<std::thread::id, ManualSchedule*>>
    ManualSchedule::schedules_;

template <typename T>
using ManualAtomic = test::DeterministicAtomicImpl<T, ManualSchedule>;
template <template <typename> class Atomic>
using TestDistributedMutex =
    detail::distributed_mutex::DistributedMutex<Atomic, false>;

/**
 * Futex extensions for ManualAtomic
 *
 * Note that doing nothing in these should still result in a program that is
 * well defined, since futex wait calls should be tolerant to spurious wakeups
 */
int futexWakeImpl(const detail::Futex<ManualAtomic>*, int, uint32_t) {
  ManualSchedule::beforeSharedAccess();
  return 1;
}
detail::FutexResult futexWaitImpl(
    const detail::Futex<ManualAtomic>*,
    uint32_t,
    std::chrono::system_clock::time_point const*,
    std::chrono::steady_clock::time_point const*,
    uint32_t) {
  ManualSchedule::beforeSharedAccess();
  return detail::FutexResult::AWOKEN;
}

template <typename Clock, typename Duration>
std::cv_status atomic_wait_until(
    const ManualAtomic<std::uintptr_t>*,
    std::uintptr_t,
    const std::chrono::time_point<Clock, Duration>&) {
  ManualSchedule::beforeSharedAccess();
  return std::cv_status::no_timeout;
}

void atomic_notify_one(const ManualAtomic<std::uintptr_t>*) {
  ManualSchedule::beforeSharedAccess();
}
} // namespace test

namespace {
constexpr auto kStressFactor = 1000;
constexpr auto kStressTestSeconds = 2;
constexpr auto kForever = 100h;

using DSched = test::DeterministicSchedule;

int sum(int n) {
  return (n * (n + 1)) / 2;
}

template <template <typename> class Atom = std::atomic>
void basicNThreads(int numThreads, int iterations = kStressFactor) {
  auto&& mutex = detail::distributed_mutex::DistributedMutex<Atom>{};
  auto&& barrier = std::atomic<int>{0};
  auto&& threads = std::vector<std::thread>{};
  auto&& result = std::vector<int>{};

  auto&& function = [&](auto id) {
    return [&, id] {
      for (auto j = 0; j < iterations; ++j) {
        auto lck = std::unique_lock<std::decay_t<decltype(mutex)>>{mutex};
        EXPECT_EQ(barrier.fetch_add(1, std::memory_order_relaxed), 0);
        std::this_thread::yield();
        result.push_back(id);
        EXPECT_EQ(barrier.fetch_sub(1, std::memory_order_relaxed), 1);
      }
    };
  };

  for (auto i = 1; i <= numThreads; ++i) {
    threads.push_back(DSched::thread(function(i)));
  }
  for (auto& thread : threads) {
    DSched::join(thread);
  }

  auto total = 0;
  for (auto value : result) {
    total += value;
  }
  EXPECT_EQ(total, sum(numThreads) * iterations);
}

template <template <typename> class Atom = std::atomic>
void lockWithTryAndTimedNThreads(
    int numThreads,
    std::chrono::seconds duration) {
  auto&& mutex = detail::distributed_mutex::DistributedMutex<Atom>{};
  auto&& barrier = std::atomic<int>{0};
  auto&& threads = std::vector<std::thread>{};
  auto&& stop = std::atomic<bool>{false};

  auto&& lockUnlockFunction = [&]() {
    while (!stop.load()) {
      auto lck = std::unique_lock<std::decay_t<decltype(mutex)>>{mutex};
      EXPECT_EQ(barrier.fetch_add(1, std::memory_order_relaxed), 0);
      std::this_thread::yield();
      EXPECT_EQ(barrier.fetch_sub(1, std::memory_order_relaxed), 1);
    }
  };

  auto tryLockFunction = [&]() {
    while (!stop.load()) {
      using Mutex = std::decay_t<decltype(mutex)>;
      auto lck = std::unique_lock<Mutex>{mutex, std::defer_lock};
      if (lck.try_lock()) {
        EXPECT_EQ(barrier.fetch_add(1, std::memory_order_relaxed), 0);
        std::this_thread::yield();
        EXPECT_EQ(barrier.fetch_sub(1, std::memory_order_relaxed), 1);
      }
    }
  };

  auto timedLockFunction = [&]() {
    while (!stop.load()) {
      using Mutex = std::decay_t<decltype(mutex)>;
      auto lck = std::unique_lock<Mutex>{mutex, std::defer_lock};
      if (lck.try_lock_for(kForever)) {
        EXPECT_EQ(barrier.fetch_add(1, std::memory_order_relaxed), 0);
        std::this_thread::yield();
        EXPECT_EQ(barrier.fetch_sub(1, std::memory_order_relaxed), 1);
      }
    }
  };

  for (auto i = 0; i < (numThreads / 3); ++i) {
    threads.push_back(DSched::thread(lockUnlockFunction));
  }
  for (auto i = 0; i < (numThreads / 3); ++i) {
    threads.push_back(DSched::thread(tryLockFunction));
  }
  for (auto i = 0; i < (numThreads / 3); ++i) {
    threads.push_back(DSched::thread(timedLockFunction));
  }

  /* sleep override */
  std::this_thread::sleep_for(duration);
  stop.store(true);
  for (auto& thread : threads) {
    DSched::join(thread);
  }
}

template <template <typename> class Atom = std::atomic>
void combineNThreads(int numThreads, std::chrono::seconds duration) {
  auto&& mutex = detail::distributed_mutex::DistributedMutex<Atom>{};
  auto&& barrier = std::atomic<int>{0};
  auto&& threads = std::vector<std::thread>{};
  auto&& stop = std::atomic<bool>{false};

  auto&& function = [&]() {
    return [&] {
      auto&& expected = std::uint64_t{0};
      auto&& local = std::atomic<std::uint64_t>{0};
      auto&& result = std::atomic<std::uint64_t>{0};
      while (!stop.load()) {
        ++expected;
        auto current = mutex.lock_combine([&]() {
          result.fetch_add(1);
          EXPECT_EQ(barrier.fetch_add(1, std::memory_order_relaxed), 0);
          EXPECT_EQ(barrier.fetch_add(1, std::memory_order_relaxed), 1);
          std::this_thread::yield();
          SCOPE_EXIT {
            EXPECT_EQ(barrier.fetch_sub(1, std::memory_order_relaxed), 1);
          };
          EXPECT_EQ(barrier.fetch_sub(1, std::memory_order_relaxed), 2);
          return local.fetch_add(1);
        });
        EXPECT_EQ(current, expected - 1);
      }

      EXPECT_EQ(expected, result.load());
    };
  };

  for (auto i = 1; i <= numThreads; ++i) {
    threads.push_back(DSched::thread(function()));
  }

  /* sleep override */
  std::this_thread::sleep_for(duration);
  stop.store(true);
  for (auto& thread : threads) {
    DSched::join(thread);
  }
}

template <template <typename> class Atom = std::atomic>
void combineWithLockNThreads(int numThreads, std::chrono::seconds duration) {
  auto&& mutex = detail::distributed_mutex::DistributedMutex<Atom>{};
  auto&& barrier = std::atomic<int>{0};
  auto&& threads = std::vector<std::thread>{};
  auto&& stop = std::atomic<bool>{false};

  auto&& lockUnlockFunction = [&]() {
    while (!stop.load()) {
      auto lck = std::unique_lock<std::decay_t<decltype(mutex)>>{mutex};
      EXPECT_EQ(barrier.fetch_add(1, std::memory_order_relaxed), 0);
      std::this_thread::yield();
      EXPECT_EQ(barrier.fetch_sub(1, std::memory_order_relaxed), 1);
    }
  };

  auto&& combineFunction = [&]() {
    auto&& expected = std::uint64_t{0};
    auto&& total = std::atomic<std::uint64_t>{0};

    while (!stop.load()) {
      ++expected;
      auto current = mutex.lock_combine([&]() {
        auto iteration = total.fetch_add(1);
        EXPECT_EQ(barrier.fetch_add(1, std::memory_order_relaxed), 0);
        EXPECT_EQ(barrier.fetch_add(1, std::memory_order_relaxed), 1);
        std::this_thread::yield();
        SCOPE_EXIT {
          EXPECT_EQ(barrier.fetch_sub(1, std::memory_order_relaxed), 1);
        };
        EXPECT_EQ(barrier.fetch_sub(1, std::memory_order_relaxed), 2);
        return iteration;
      });

      EXPECT_EQ(expected, current + 1);
    }

    EXPECT_EQ(expected, total.load());
  };

  for (auto i = 1; i < (numThreads / 2); ++i) {
    threads.push_back(DSched::thread(combineFunction));
  }
  for (auto i = 0; i < (numThreads / 2); ++i) {
    threads.push_back(DSched::thread(lockUnlockFunction));
  }

  /* sleep override */
  std::this_thread::sleep_for(duration);
  stop.store(true);
  for (auto& thread : threads) {
    DSched::join(thread);
  }
}

template <template <typename> class Atom = std::atomic>
void combineWithTryLockNThreads(int numThreads, std::chrono::seconds duration) {
  auto&& mutex = detail::distributed_mutex::DistributedMutex<Atom>{};
  auto&& barrier = std::atomic<int>{0};
  auto&& threads = std::vector<std::thread>{};
  auto&& stop = std::atomic<bool>{false};

  auto&& lockUnlockFunction = [&]() {
    while (!stop.load()) {
      auto lck = std::unique_lock<std::decay_t<decltype(mutex)>>{mutex};
      EXPECT_EQ(barrier.fetch_add(1, std::memory_order_relaxed), 0);
      std::this_thread::yield();
      EXPECT_EQ(barrier.fetch_sub(1, std::memory_order_relaxed), 1);
    }
  };

  auto&& combineFunction = [&]() {
    auto&& expected = std::uint64_t{0};
    auto&& total = std::atomic<std::uint64_t>{0};

    while (!stop.load()) {
      ++expected;
      auto current = mutex.lock_combine([&]() {
        auto iteration = total.fetch_add(1);
        EXPECT_EQ(barrier.fetch_add(1, std::memory_order_relaxed), 0);
        EXPECT_EQ(barrier.fetch_add(1, std::memory_order_relaxed), 1);
        std::this_thread::yield();
        SCOPE_EXIT {
          EXPECT_EQ(barrier.fetch_sub(1, std::memory_order_relaxed), 1);
        };
        EXPECT_EQ(barrier.fetch_sub(1, std::memory_order_relaxed), 2);
        return iteration;
      });

      EXPECT_EQ(expected, current + 1);
    }

    EXPECT_EQ(expected, total.load());
  };

  auto tryLockFunction = [&]() {
    while (!stop.load()) {
      using Mutex = std::decay_t<decltype(mutex)>;
      auto lck = std::unique_lock<Mutex>{mutex, std::defer_lock};
      if (lck.try_lock()) {
        EXPECT_EQ(barrier.fetch_add(1, std::memory_order_relaxed), 0);
        std::this_thread::yield();
        EXPECT_EQ(barrier.fetch_sub(1, std::memory_order_relaxed), 1);
      }
    }
  };

  for (auto i = 0; i < (numThreads / 3); ++i) {
    threads.push_back(DSched::thread(lockUnlockFunction));
  }
  for (auto i = 0; i < (numThreads / 3); ++i) {
    threads.push_back(DSched::thread(combineFunction));
  }
  for (auto i = 0; i < (numThreads / 3); ++i) {
    threads.push_back(DSched::thread(tryLockFunction));
  }

  /* sleep override */
  std::this_thread::sleep_for(duration);
  stop.store(true);
  for (auto& thread : threads) {
    DSched::join(thread);
  }
}

template <template <typename> class Atom = std::atomic>
void combineWithLockTryAndTimedNThreads(
    int numThreads,
    std::chrono::seconds duration) {
  auto&& mutex = detail::distributed_mutex::DistributedMutex<Atom>{};
  auto&& barrier = std::atomic<int>{0};
  auto&& threads = std::vector<std::thread>{};
  auto&& stop = std::atomic<bool>{false};

  auto&& lockUnlockFunction = [&]() {
    while (!stop.load()) {
      auto lck = std::unique_lock<std::decay_t<decltype(mutex)>>{mutex};
      EXPECT_EQ(barrier.fetch_add(1, std::memory_order_relaxed), 0);
      std::this_thread::yield();
      EXPECT_EQ(barrier.fetch_sub(1, std::memory_order_relaxed), 1);
    }
  };

  auto&& combineFunction = [&]() {
    auto&& expected = std::uint64_t{0};
    auto&& total = std::atomic<std::uint64_t>{0};

    while (!stop.load()) {
      ++expected;
      auto current = mutex.lock_combine([&]() {
        auto iteration = total.fetch_add(1);
        EXPECT_EQ(barrier.fetch_add(1, std::memory_order_relaxed), 0);
        EXPECT_EQ(barrier.fetch_add(1, std::memory_order_relaxed), 1);
        std::this_thread::yield();
        SCOPE_EXIT {
          EXPECT_EQ(barrier.fetch_sub(1, std::memory_order_relaxed), 1);
        };
        EXPECT_EQ(barrier.fetch_sub(1, std::memory_order_relaxed), 2);

        // return a non-trivially-copyable object that occupies all the
        // storage we use to coalesce returns to test that codepath
        return folly::make_array(
            iteration,
            iteration + 1,
            iteration + 2,
            iteration + 3,
            iteration + 4,
            iteration + 5);
      });

      EXPECT_EQ(expected, current[0] + 1);
      EXPECT_EQ(expected, current[1]);
      EXPECT_EQ(expected, current[2] - 1);
      EXPECT_EQ(expected, current[3] - 2);
      EXPECT_EQ(expected, current[4] - 3);
      EXPECT_EQ(expected, current[5] - 4);
    }

    EXPECT_EQ(expected, total.load());
  };

  auto tryLockFunction = [&]() {
    while (!stop.load()) {
      using Mutex = std::decay_t<decltype(mutex)>;
      auto lck = std::unique_lock<Mutex>{mutex, std::defer_lock};
      if (lck.try_lock()) {
        EXPECT_EQ(barrier.fetch_add(1, std::memory_order_relaxed), 0);
        std::this_thread::yield();
        EXPECT_EQ(barrier.fetch_sub(1, std::memory_order_relaxed), 1);
      }
    }
  };

  auto timedLockFunction = [&]() {
    while (!stop.load()) {
      using Mutex = std::decay_t<decltype(mutex)>;
      auto lck = std::unique_lock<Mutex>{mutex, std::defer_lock};
      if (lck.try_lock_for(kForever)) {
        EXPECT_EQ(barrier.fetch_add(1, std::memory_order_relaxed), 0);
        std::this_thread::yield();
        EXPECT_EQ(barrier.fetch_sub(1, std::memory_order_relaxed), 1);
      }
    }
  };

  for (auto i = 0; i < (numThreads / 4); ++i) {
    threads.push_back(DSched::thread(lockUnlockFunction));
  }
  for (auto i = 0; i < (numThreads / 4); ++i) {
    threads.push_back(DSched::thread(combineFunction));
  }
  for (auto i = 0; i < (numThreads / 4); ++i) {
    threads.push_back(DSched::thread(tryLockFunction));
  }
  for (auto i = 0; i < (numThreads / 4); ++i) {
    threads.push_back(DSched::thread(timedLockFunction));
  }

  /* sleep override */
  std::this_thread::sleep_for(duration);
  stop.store(true);
  for (auto& thread : threads) {
    DSched::join(thread);
  }
}
} // namespace

TEST(DistributedMutex, InternalDetailTestOne) {
  auto value = 0;
  auto ptr = reinterpret_cast<std::uintptr_t>(&value);
  EXPECT_EQ(detail::distributed_mutex::extractPtr<int>(ptr), &value);
  ptr = ptr | 0b1;
  EXPECT_EQ(detail::distributed_mutex::extractPtr<int>(ptr), &value);
}

TEST(DistributedMutex, Basic) {
  auto&& mutex = DistributedMutex{};
  auto state = mutex.lock();
  mutex.unlock(std::move(state));
}

TEST(DistributedMutex, BasicTryLock) {
  auto&& mutex = DistributedMutex{};

  while (true) {
    auto state = mutex.try_lock();
    if (state) {
      mutex.unlock(std::move(state));
      break;
    }
  }
}

TEST(DistributedMutex, TestSingleElementContentionChain) {
  // Acquire the mutex once, let another thread form a contention chain on the
  // mutex, and then release it.  Observe the other thread grab the lock
  auto&& schedule = test::ManualSchedule{};
  auto&& mutex = test::TestDistributedMutex<test::ManualAtomic>{};

  auto&& waiter = std::thread{[&]() {
    schedule.setCallback([&, i = 0]() mutable {
      if (i == 2) {
        schedule.post(1);
      }
      ++i;
    });

    schedule.wait(0);
    auto state = mutex.lock();
    mutex.unlock(std::move(state));
  }};

  // lock the mutex, signal the waiter, and then wait till the first thread
  // has gotten on the wait list
  auto state = mutex.lock();
  schedule.post(0);
  schedule.wait(1);

  // release the mutex, and then wait for the waiter to acquire the lock
  mutex.unlock(std::move(state));
  waiter.join();
}

TEST(DistributedMutex, TestTwoElementContentionChain) {
  // Acquire the mutex once, let another thread form a contention chain on the
  // mutex, and then release it.  Observe the other thread grab the lock
  auto&& schedule = test::ManualSchedule{};
  auto&& mutex = test::TestDistributedMutex<test::ManualAtomic>{};

  auto&& one = std::thread{[&]() {
    schedule.setCallback([&, i = 0]() mutable {
      if (i == 2) {
        schedule.post(3);
      }
      ++i;
    });

    schedule.wait(0);
    auto state = mutex.lock();
    mutex.unlock(std::move(state));
  }};

  auto&& two = std::thread{[&]() {
    schedule.setCallback([&, i = 0]() mutable {
      if (i == 2) {
        schedule.post(2);
      }
      ++i;
    });

    schedule.wait(1);
    auto state = mutex.lock();
    mutex.unlock(std::move(state));
  }};

  // lock the mutex, signal the waiter, and then wait till the first thread
  // has gotten on the wait list
  auto state = mutex.lock();
  schedule.post(0);
  schedule.post(1);
  schedule.wait(2);
  schedule.wait(3);

  // release the mutex, and then wait for the waiter to acquire the lock
  mutex.unlock(std::move(state));
  one.join();
  two.join();
}

TEST(DistributedMutex, TestTwoContentionChains) {
  auto&& schedule = test::ManualSchedule{};
  auto&& mutex = test::TestDistributedMutex<test::ManualAtomic>{};

  auto&& one = std::thread{[&]() {
    schedule.setCallback([&, i = 0]() mutable {
      if (i == 2) {
        schedule.post(0);
      }
      ++i;
    });

    schedule.wait(1);
    auto state = mutex.lock();
    schedule.wait(4);
    mutex.unlock(std::move(state));
  }};
  auto&& two = std::thread{[&]() {
    schedule.setCallback([&, i = 0]() mutable {
      if (i == 2) {
        schedule.post(2);
      }
      ++i;
    });

    schedule.wait(3);
    auto state = mutex.lock();
    schedule.wait(5);
    mutex.unlock(std::move(state));
  }};

  auto state = mutex.lock();
  schedule.post(1);
  schedule.post(3);
  schedule.wait(0);
  schedule.wait(2);

  // at this point there is one contention chain.  Release it
  mutex.unlock(std::move(state));

  // then start a new contention chain
  auto&& three = std::thread{[&]() {
    schedule.setCallback([&, i = 0]() mutable {
      if (i == 2) {
        schedule.post(4);
        schedule.post(5);
      }
      ++i;
    });

    auto lockState = mutex.lock();
    schedule.post(6);
    mutex.unlock(std::move(lockState));
  }};

  // wait for the third thread to pick up the lock
  schedule.wait(6);
  one.join();
  two.join();
  three.join();
}

TEST(DistributedMutex, StressTwoThreads) {
  basicNThreads(2);
}
TEST(DistributedMutex, StressThreeThreads) {
  basicNThreads(3);
}
TEST(DistributedMutex, StressFourThreads) {
  basicNThreads(4);
}
TEST(DistributedMutex, StressFiveThreads) {
  basicNThreads(5);
}
TEST(DistributedMutex, StressSixThreads) {
  basicNThreads(6);
}
TEST(DistributedMutex, StressSevenThreads) {
  basicNThreads(7);
}
TEST(DistributedMutex, StressEightThreads) {
  basicNThreads(8);
}
TEST(DistributedMutex, StressSixteenThreads) {
  basicNThreads(16);
}
TEST(DistributedMutex, StressThirtyTwoThreads) {
  basicNThreads(32);
}
TEST(DistributedMutex, StressSixtyFourThreads) {
  basicNThreads(64);
}
TEST(DistributedMutex, StressHundredThreads) {
  basicNThreads(100);
}
TEST(DistributedMutex, StressHardwareConcurrencyThreads) {
  basicNThreads(std::thread::hardware_concurrency());
}

TEST(DistributedMutex, StressThreeThreadsLockTryAndTimed) {
  lockWithTryAndTimedNThreads(3, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, StressSixThreadsLockTryAndTimed) {
  lockWithTryAndTimedNThreads(6, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, StressTwelveThreadsLockTryAndTimed) {
  lockWithTryAndTimedNThreads(12, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, StressTwentyFourThreadsLockTryAndTimed) {
  lockWithTryAndTimedNThreads(24, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, StressFourtyEightThreadsLockTryAndTimed) {
  lockWithTryAndTimedNThreads(48, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, StressSixtyFourThreadsLockTryAndTimed) {
  lockWithTryAndTimedNThreads(64, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, StressHwConcThreadsLockTryAndTimed) {
  lockWithTryAndTimedNThreads(
      std::thread::hardware_concurrency(),
      std::chrono::seconds{kStressTestSeconds});
}

TEST(DistributedMutex, StressTwoThreadsCombine) {
  combineNThreads(2, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, StressThreeThreadsCombine) {
  combineNThreads(3, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, StressFourThreadsCombine) {
  combineNThreads(4, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, StressFiveThreadsCombine) {
  combineNThreads(5, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, StressSixThreadsCombine) {
  combineNThreads(6, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, StressSevenThreadsCombine) {
  combineNThreads(7, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, StressEightThreadsCombine) {
  combineNThreads(8, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, StressSixteenThreadsCombine) {
  combineNThreads(16, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, StressThirtyTwoThreadsCombine) {
  combineNThreads(32, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, StressSixtyFourThreadsCombine) {
  combineNThreads(64, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, StressHundredThreadsCombine) {
  combineNThreads(100, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, StressHardwareConcurrencyThreadsCombine) {
  combineNThreads(
      std::thread::hardware_concurrency(),
      std::chrono::seconds{kStressTestSeconds});
}

TEST(DistributedMutex, StressTwoThreadsCombineAndLock) {
  combineWithLockNThreads(2, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, StressFourThreadsCombineAndLock) {
  combineWithLockNThreads(4, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, StressEightThreadsCombineAndLock) {
  combineWithLockNThreads(8, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, StressSixteenThreadsCombineAndLock) {
  combineWithLockNThreads(16, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, StressThirtyTwoThreadsCombineAndLock) {
  combineWithLockNThreads(32, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, StressSixtyFourThreadsCombineAndLock) {
  combineWithLockNThreads(64, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, StressHardwareConcurrencyThreadsCombineAndLock) {
  combineWithLockNThreads(
      std::thread::hardware_concurrency(),
      std::chrono::seconds{kStressTestSeconds});
}

TEST(DistributedMutex, StressThreeThreadsCombineTryLockAndLock) {
  combineWithTryLockNThreads(3, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, StressSixThreadsCombineTryLockAndLock) {
  combineWithTryLockNThreads(6, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, StressTwelveThreadsCombineTryLockAndLock) {
  combineWithTryLockNThreads(12, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, StressTwentyFourThreadsCombineTryLockAndLock) {
  combineWithTryLockNThreads(24, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, StressFourtyEightThreadsCombineTryLockAndLock) {
  combineWithTryLockNThreads(48, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, StressSixtyFourThreadsCombineTryLockAndLock) {
  combineWithTryLockNThreads(64, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, StressHardwareConcurrencyThreadsCombineTryLockAndLock) {
  combineWithTryLockNThreads(
      std::thread::hardware_concurrency(),
      std::chrono::seconds{kStressTestSeconds});
}

TEST(DistributedMutex, StressThreeThreadsCombineTryLockLockAndTimed) {
  combineWithLockTryAndTimedNThreads(
      3, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, StressSixThreadsCombineTryLockLockAndTimed) {
  combineWithLockTryAndTimedNThreads(
      6, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, StressTwelveThreadsCombineTryLockLockAndTimed) {
  combineWithLockTryAndTimedNThreads(
      12, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, StressTwentyFourThreadsCombineTryLockLockAndTimed) {
  combineWithLockTryAndTimedNThreads(
      24, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, StressFourtyEightThreadsCombineTryLockLockAndTimed) {
  combineWithLockTryAndTimedNThreads(
      48, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, StressSixtyFourThreadsCombineTryLockLockAndTimed) {
  combineWithLockTryAndTimedNThreads(
      64, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, StressHwConcurrencyThreadsCombineTryLockLockAndTimed) {
  combineWithLockTryAndTimedNThreads(
      std::thread::hardware_concurrency(),
      std::chrono::seconds{kStressTestSeconds});
}

TEST(DistributedMutex, StressTryLock) {
  auto&& mutex = DistributedMutex{};

  for (auto i = 0; i < kStressFactor; ++i) {
    while (true) {
      auto state = mutex.try_lock();
      if (state) {
        mutex.unlock(std::move(state));
        break;
      }
    }
  }
}

namespace {
constexpr auto numIterationsDeterministicTest(int threads) {
  if (threads <= 8) {
    return 100;
  }

  return 10;
}

void runBasicNThreadsDeterministic(int threads, int iterations) {
  for (auto pass = 0; pass < 3; ++pass) {
    auto&& schedule = DSched{DSched::uniform(pass)};
    basicNThreads<test::DeterministicAtomic>(threads, iterations);
    static_cast<void>(schedule);
  }
}

void combineNThreadsDeterministic(int threads, std::chrono::seconds t) {
  const auto kNumPasses = 3.0;
  const auto seconds = std::ceil(static_cast<double>(t.count()) / kNumPasses);
  const auto time = std::chrono::seconds{static_cast<std::uint64_t>(seconds)};

  for (auto pass = 0; pass < kNumPasses; ++pass) {
    auto&& schedule = DSched{DSched::uniform(pass)};
    combineNThreads<test::DeterministicAtomic>(threads, time);
    static_cast<void>(schedule);
  }
}

void combineAndLockNThreadsDeterministic(int threads, std::chrono::seconds t) {
  const auto kNumPasses = 3.0;
  const auto seconds = std::ceil(static_cast<double>(t.count()) / kNumPasses);
  const auto time = std::chrono::seconds{static_cast<std::uint64_t>(seconds)};

  for (auto pass = 0; pass < kNumPasses; ++pass) {
    auto&& schedule = DSched{DSched::uniform(pass)};
    combineWithLockNThreads<test::DeterministicAtomic>(threads, time);
    static_cast<void>(schedule);
  }
}

void combineTryLockAndLockNThreadsDeterministic(
    int threads,
    std::chrono::seconds t) {
  const auto kNumPasses = 3.0;
  const auto seconds = std::ceil(static_cast<double>(t.count()) / kNumPasses);
  const auto time = std::chrono::seconds{static_cast<std::uint64_t>(seconds)};

  for (auto pass = 0; pass < kNumPasses; ++pass) {
    auto&& schedule = DSched{DSched::uniform(pass)};
    combineWithTryLockNThreads<test::DeterministicAtomic>(threads, time);
    static_cast<void>(schedule);
  }
}

void lockWithTryAndTimedNThreadsDeterministic(
    int threads,
    std::chrono::seconds t) {
  const auto kNumPasses = 3.0;
  const auto seconds = std::ceil(static_cast<double>(t.count()) / kNumPasses);
  const auto time = std::chrono::seconds{static_cast<std::uint64_t>(seconds)};

  for (auto pass = 0; pass < kNumPasses; ++pass) {
    auto&& schedule = DSched{DSched::uniform(pass)};
    lockWithTryAndTimedNThreads<test::DeterministicAtomic>(threads, time);
    static_cast<void>(schedule);
  }
}

void combineWithTryLockAndTimedNThreadsDeterministic(
    int threads,
    std::chrono::seconds t) {
  const auto kNumPasses = 3.0;
  const auto seconds = std::ceil(static_cast<double>(t.count()) / kNumPasses);
  const auto time = std::chrono::seconds{static_cast<std::uint64_t>(seconds)};

  for (auto pass = 0; pass < kNumPasses; ++pass) {
    auto&& schedule = DSched{DSched::uniform(pass)};
    combineWithLockTryAndTimedNThreads<test::DeterministicAtomic>(
        threads, time);
    static_cast<void>(schedule);
  }
}
} // namespace

TEST(DistributedMutex, DeterministicStressTwoThreads) {
  runBasicNThreadsDeterministic(2, numIterationsDeterministicTest(2));
}
TEST(DistributedMutex, DeterministicStressFourThreads) {
  runBasicNThreadsDeterministic(4, numIterationsDeterministicTest(4));
}
TEST(DistributedMutex, DeterministicStressEightThreads) {
  runBasicNThreadsDeterministic(8, numIterationsDeterministicTest(8));
}
TEST(DistributedMutex, DeterministicStressSixteenThreads) {
  runBasicNThreadsDeterministic(16, numIterationsDeterministicTest(16));
}
TEST(DistributedMutex, DeterministicStressThirtyTwoThreads) {
  runBasicNThreadsDeterministic(32, numIterationsDeterministicTest(32));
}

TEST(DistributedMutex, DeterministicStressThreeThreadsLockTryAndTimed) {
  lockWithTryAndTimedNThreadsDeterministic(
      3, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, DeterministicStressSixThreadsLockTryAndTimed) {
  lockWithTryAndTimedNThreadsDeterministic(
      6, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, DeterministicStressTwelveThreadsLockTryAndTimed) {
  lockWithTryAndTimedNThreadsDeterministic(
      12, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, DeterministicStressTwentyFourThreadsLockTryAndTimed) {
  lockWithTryAndTimedNThreadsDeterministic(
      24, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, DeterministicStressFourtyEightThreadsLockTryAndTimed) {
  lockWithTryAndTimedNThreadsDeterministic(
      48, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, DeterministicStressSixtyFourThreadsLockTryAndTimed) {
  lockWithTryAndTimedNThreadsDeterministic(
      64, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, DeterministicStressHwConcThreadsLockTryAndTimed) {
  lockWithTryAndTimedNThreadsDeterministic(
      std::thread::hardware_concurrency(),
      std::chrono::seconds{kStressTestSeconds});
}

TEST(DistributedMutex, CombineDeterministicStressTwoThreads) {
  combineNThreadsDeterministic(2, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, CombineDeterministicStressFourThreads) {
  combineNThreadsDeterministic(4, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, CombineDeterministicStressEightThreads) {
  combineNThreadsDeterministic(8, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, CombineDeterministicStressSixteenThreads) {
  combineNThreadsDeterministic(16, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, CombineDeterministicStressThirtyTwoThreads) {
  combineNThreadsDeterministic(32, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, CombineDeterministicStressSixtyFourThreads) {
  combineNThreadsDeterministic(64, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, CombineDeterministicStressHardwareConcurrencyThreads) {
  combineNThreadsDeterministic(
      std::thread::hardware_concurrency(),
      std::chrono::seconds{kStressTestSeconds});
}

TEST(DistributedMutex, CombineAndLockDeterministicStressTwoThreads) {
  combineAndLockNThreadsDeterministic(
      2, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, CombineAndLockDeterministicStressFourThreads) {
  combineAndLockNThreadsDeterministic(
      4, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, CombineAndLockDeterministicStressEightThreads) {
  combineAndLockNThreadsDeterministic(
      8, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, CombineAndLockDeterministicStressSixteenThreads) {
  combineAndLockNThreadsDeterministic(
      16, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, CombineAndLockDeterministicStressThirtyTwoThreads) {
  combineAndLockNThreadsDeterministic(
      32, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, CombineAndLockDeterministicStressSixtyFourThreads) {
  combineAndLockNThreadsDeterministic(
      64, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, CombineAndLockDeterministicStressHWConcurrencyThreads) {
  combineAndLockNThreadsDeterministic(
      std::thread::hardware_concurrency(),
      std::chrono::seconds{kStressTestSeconds});
}

TEST(DistributedMutex, CombineTryLockAndLockDeterministicStressThreeThreads) {
  combineTryLockAndLockNThreadsDeterministic(
      3, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, CombineTryLockAndLockDeterministicStressSixThreads) {
  combineTryLockAndLockNThreadsDeterministic(
      6, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, CombineTryLockAndLockDeterministicStressTwelveThreads) {
  combineTryLockAndLockNThreadsDeterministic(
      12, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, CombineTryLockAndLockDeterministicStressTwentyThreads) {
  combineTryLockAndLockNThreadsDeterministic(
      24, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, CombineTryLockAndLockDeterministicStressFortyThreads) {
  combineTryLockAndLockNThreadsDeterministic(
      48, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, CombineTryLockAndLockDeterministicStressSixtyThreads) {
  combineTryLockAndLockNThreadsDeterministic(
      64, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, CombineTryLockAndLockDeterministicStressHWConcThreads) {
  combineTryLockAndLockNThreadsDeterministic(
      std::thread::hardware_concurrency(),
      std::chrono::seconds{kStressTestSeconds});
}

TEST(DistributedMutex, CombineTryLockAndTimedDeterministicStressThreeThreads) {
  combineWithTryLockAndTimedNThreadsDeterministic(
      3, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, CombineTryLockAndTimedDeterministicStressSixThreads) {
  combineWithTryLockAndTimedNThreadsDeterministic(
      6, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, CombineTryLockAndTimedDeterministicStressTwelveThreads) {
  combineWithTryLockAndTimedNThreadsDeterministic(
      12, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, CombineTryLockAndTimedDeterministicStressTwentyThreads) {
  combineWithTryLockAndTimedNThreadsDeterministic(
      24, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, CombineTryLockAndTimedDeterministicStressFortyThreads) {
  combineWithTryLockAndTimedNThreadsDeterministic(
      48, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, CombineTryLockAndTimedDeterministicStressSixtyThreads) {
  combineWithTryLockAndTimedNThreadsDeterministic(
      64, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, CombineTryLockAndTimedDeterministicStressHWConcThreads) {
  combineWithTryLockAndTimedNThreadsDeterministic(
      std::thread::hardware_concurrency(),
      std::chrono::seconds{kStressTestSeconds});
}

TEST(DistributedMutex, TimedLockTimeout) {
  auto&& mutex = DistributedMutex{};
  auto&& start = folly::Baton<>{};
  auto&& done = folly::Baton<>{};

  auto thread = std::thread{[&]() {
    auto state = mutex.lock();
    start.post();
    done.wait();
    mutex.unlock(std::move(state));
  }};

  start.wait();
  auto result = mutex.try_lock_for(10ms);
  EXPECT_FALSE(result);
  done.post();
  thread.join();
}

TEST(DistributedMutex, TimedLockAcquireAfterUnlock) {
  auto&& mutex = DistributedMutex{};
  auto&& start = folly::Baton<>{};

  auto thread = std::thread{[&]() {
    auto state = mutex.lock();
    start.post();
    /* sleep override */
    std::this_thread::sleep_for(10ms);
    mutex.unlock(std::move(state));
  }};

  start.wait();
  auto result = mutex.try_lock_for(kForever);
  EXPECT_TRUE(result);
  thread.join();
}

TEST(DistributedMutex, TimedLockAcquireAfterLock) {
  auto&& mutex = test::TestDistributedMutex<test::ManualAtomic>{};
  auto&& schedule = test::ManualSchedule{};

  auto thread = std::thread{[&] {
    schedule.setCallback([&, i = 0]() mutable {
      if (i == 1) {
        schedule.post(0);
        schedule.wait(1);
      }

      // when this thread goes into the atomic_notify_one() we let the other
      // thread wake up
      if (i == 3) {
        schedule.post(2);
      }

      ++i;
    });

    auto state = mutex.lock();
    mutex.unlock(std::move(state));
  }};

  schedule.setCallback([&, i = 0]() mutable {
    // allow the other thread to unlock after the current thread has set the
    // timed waiter state into the mutex
    if (i == 2) {
      schedule.post(1);
      schedule.wait(2);
    }
    ++i;
  });
  schedule.wait(0);
  auto state = mutex.try_lock_for(kForever);
  EXPECT_TRUE(state);
  mutex.unlock(std::move(state));
  thread.join();
}

TEST(DistributedMutex, TimedLockAcquireAfterContentionChain) {
  auto&& mutex = test::TestDistributedMutex<test::ManualAtomic>{};
  auto&& schedule = test::ManualSchedule{};

  auto one = std::thread{[&] {
    schedule.setCallback([&, i = 0]() mutable {
      if (i == 1) {
        schedule.post(0);
        schedule.wait(1);
        schedule.wait(2);
      }
      ++i;
    });

    auto state = mutex.lock();
    mutex.unlock(std::move(state));
  }};
  auto two = std::thread{[&] {
    schedule.setCallback([&, i = 0]() mutable {
      // block the current thread until the first thread has acquired the
      // lock
      if (i == 0) {
        schedule.wait(0);
      }

      // when the current thread enqueues, let the first thread unlock so we
      // get woken up
      //
      // then wait for the first thread to unlock
      if (i == 2) {
        schedule.post(1);
      }

      ++i;
    });

    auto state = mutex.lock();
    mutex.unlock(std::move(state));
  }};

  // make the current thread wait for the first thread to unlock
  schedule.setCallback([&, i = 0]() mutable {
    // let the first thread unlock after we have enqueued ourselves on the
    // mutex
    if (i == 2) {
      schedule.post(2);
    }
    ++i;
  });
  auto state = mutex.try_lock_for(kForever);
  EXPECT_TRUE(state);
  mutex.unlock(std::move(state));

  one.join();
  two.join();
}

namespace {
template <template <typename> class Atom = std::atomic>
void stressTryLockWithConcurrentLocks(
    int numThreads,
    int iterations = kStressFactor) {
  auto&& threads = std::vector<std::thread>{};
  auto&& mutex = detail::distributed_mutex::DistributedMutex<Atom>{};
  auto&& atomic = std::atomic<std::uint64_t>{0};

  for (auto i = 0; i < numThreads; ++i) {
    threads.push_back(DSched::thread([&] {
      for (auto j = 0; j < iterations; ++j) {
        auto state = mutex.lock();
        EXPECT_EQ(atomic.fetch_add(1, std::memory_order_relaxed), 0);
        EXPECT_EQ(atomic.fetch_sub(1, std::memory_order_relaxed), 1);
        mutex.unlock(std::move(state));
      }
    }));
  }

  for (auto i = 0; i < iterations; ++i) {
    if (auto state = mutex.try_lock()) {
      EXPECT_EQ(atomic.fetch_add(1, std::memory_order_relaxed), 0);
      EXPECT_EQ(atomic.fetch_sub(1, std::memory_order_relaxed), 1);
      mutex.unlock(std::move(state));
    }
  }

  for (auto& thread : threads) {
    DSched::join(thread);
  }
}
} // namespace

TEST(DistributedMutex, StressTryLockWithConcurrentLocksTwoThreads) {
  stressTryLockWithConcurrentLocks(2);
}
TEST(DistributedMutex, StressTryLockWithConcurrentLocksFourThreads) {
  stressTryLockWithConcurrentLocks(4);
}
TEST(DistributedMutex, StressTryLockWithConcurrentLocksEightThreads) {
  stressTryLockWithConcurrentLocks(8);
}
TEST(DistributedMutex, StressTryLockWithConcurrentLocksSixteenThreads) {
  stressTryLockWithConcurrentLocks(16);
}
TEST(DistributedMutex, StressTryLockWithConcurrentLocksThirtyTwoThreads) {
  stressTryLockWithConcurrentLocks(32);
}
TEST(DistributedMutex, StressTryLockWithConcurrentLocksSixtyFourThreads) {
  stressTryLockWithConcurrentLocks(64);
}

TEST(DistributedMutex, DeterministicTryLockWithLocksTwoThreads) {
  auto iterations = numIterationsDeterministicTest(2);
  stressTryLockWithConcurrentLocks(2, iterations);

  for (auto pass = 0; pass < 3; ++pass) {
    auto&& schedule = DSched{DSched::uniform(pass)};
    stressTryLockWithConcurrentLocks<test::DeterministicAtomic>(2, iterations);
    static_cast<void>(schedule);
  }
}
TEST(DistributedMutex, DeterministicTryLockWithFourThreads) {
  auto iterations = numIterationsDeterministicTest(4);
  stressTryLockWithConcurrentLocks(4, iterations);

  for (auto pass = 0; pass < 3; ++pass) {
    auto&& schedule = DSched{DSched::uniform(pass)};
    stressTryLockWithConcurrentLocks<test::DeterministicAtomic>(4, iterations);
    static_cast<void>(schedule);
  }
}
TEST(DistributedMutex, DeterministicTryLockWithLocksEightThreads) {
  auto iterations = numIterationsDeterministicTest(8);
  stressTryLockWithConcurrentLocks(8, iterations);

  for (auto pass = 0; pass < 3; ++pass) {
    auto&& schedule = DSched{DSched::uniform(pass)};
    stressTryLockWithConcurrentLocks<test::DeterministicAtomic>(8, iterations);
    static_cast<void>(schedule);
  }
}
TEST(DistributedMutex, DeterministicTryLockWithLocksSixteenThreads) {
  auto iterations = numIterationsDeterministicTest(16);
  stressTryLockWithConcurrentLocks(16, iterations);

  for (auto pass = 0; pass < 3; ++pass) {
    auto&& schedule = DSched{DSched::uniform(pass)};
    stressTryLockWithConcurrentLocks<test::DeterministicAtomic>(16, iterations);
    static_cast<void>(schedule);
  }
}
TEST(DistributedMutex, DeterministicTryLockWithLocksThirtyTwoThreads) {
  auto iterations = numIterationsDeterministicTest(32);
  stressTryLockWithConcurrentLocks(32, iterations);

  for (auto pass = 0; pass < 3; ++pass) {
    auto&& schedule = DSched{DSched::uniform(pass)};
    stressTryLockWithConcurrentLocks<test::DeterministicAtomic>(32, iterations);
    static_cast<void>(schedule);
  }
}
TEST(DistributedMutex, DeterministicTryLockWithLocksSixtyFourThreads) {
  stressTryLockWithConcurrentLocks(64, 5);

  for (auto pass = 0; pass < 3; ++pass) {
    auto&& schedule = DSched{DSched::uniform(pass)};
    stressTryLockWithConcurrentLocks<test::DeterministicAtomic>(64, 5);
    static_cast<void>(schedule);
  }
}

namespace {
template <template <typename> class Atom = std::atomic>
void concurrentTryLocks(int numThreads, int iterations = kStressFactor) {
  auto&& threads = std::vector<std::thread>{};
  auto&& mutex = detail::distributed_mutex::DistributedMutex<Atom>{};
  auto&& atomic = std::atomic<std::uint64_t>{0};

  for (auto i = 0; i < numThreads; ++i) {
    threads.push_back(DSched::thread([&] {
      for (auto j = 0; j < iterations; ++j) {
        if (auto state = mutex.try_lock()) {
          EXPECT_EQ(atomic.fetch_add(1, std::memory_order_relaxed), 0);
          EXPECT_EQ(atomic.fetch_sub(1, std::memory_order_relaxed), 1);
          mutex.unlock(std::move(state));
        }
      }
    }));
  }

  for (auto& thread : threads) {
    DSched::join(thread);
  }
}
} // namespace

TEST(DistributedMutex, StressTryLockWithTwoThreads) {
  concurrentTryLocks(2);
}
TEST(DistributedMutex, StressTryLockFourThreads) {
  concurrentTryLocks(4);
}
TEST(DistributedMutex, StressTryLockEightThreads) {
  concurrentTryLocks(8);
}
TEST(DistributedMutex, StressTryLockSixteenThreads) {
  concurrentTryLocks(16);
}
TEST(DistributedMutex, StressTryLockThirtyTwoThreads) {
  concurrentTryLocks(32);
}
TEST(DistributedMutex, StressTryLockSixtyFourThreads) {
  concurrentTryLocks(64);
}

TEST(DistributedMutex, DeterministicTryLockTwoThreads) {
  auto iterations = numIterationsDeterministicTest(2);
  concurrentTryLocks(2, iterations);

  for (auto pass = 0; pass < 3; ++pass) {
    auto&& schedule = DSched{DSched::uniform(pass)};
    concurrentTryLocks<test::DeterministicAtomic>(2, iterations);
    static_cast<void>(schedule);
  }
}
TEST(DistributedMutex, DeterministicTryLockFourThreads) {
  auto iterations = numIterationsDeterministicTest(4);
  concurrentTryLocks(4, iterations);

  for (auto pass = 0; pass < 3; ++pass) {
    auto&& schedule = DSched{DSched::uniform(pass)};
    concurrentTryLocks<test::DeterministicAtomic>(4, iterations);
    static_cast<void>(schedule);
  }
}
TEST(DistributedMutex, DeterministicTryLockEightThreads) {
  auto iterations = numIterationsDeterministicTest(8);
  concurrentTryLocks(8, iterations);

  for (auto pass = 0; pass < 3; ++pass) {
    auto&& schedule = DSched{DSched::uniform(pass)};
    concurrentTryLocks<test::DeterministicAtomic>(8, iterations);
    static_cast<void>(schedule);
  }
}
TEST(DistributedMutex, DeterministicTryLockSixteenThreads) {
  auto iterations = numIterationsDeterministicTest(16);
  concurrentTryLocks(16, iterations);

  for (auto pass = 0; pass < 3; ++pass) {
    auto&& schedule = DSched{DSched::uniform(pass)};
    concurrentTryLocks<test::DeterministicAtomic>(16, iterations);
    static_cast<void>(schedule);
  }
}
TEST(DistributedMutex, DeterministicTryLockThirtyTwoThreads) {
  auto iterations = numIterationsDeterministicTest(32);
  concurrentTryLocks(32, iterations);

  for (auto pass = 0; pass < 3; ++pass) {
    auto&& schedule = DSched{DSched::uniform(pass)};
    concurrentTryLocks<test::DeterministicAtomic>(32, iterations);
    static_cast<void>(schedule);
  }
}
TEST(DistributedMutex, DeterministicTryLockSixtyFourThreads) {
  concurrentTryLocks(64, 5);

  for (auto pass = 0; pass < 3; ++pass) {
    auto&& schedule = DSched{DSched::uniform(pass)};
    concurrentTryLocks<test::DeterministicAtomic>(64, 5);
    static_cast<void>(schedule);
  }
}

namespace {
class TestConstruction {
 public:
  TestConstruction() = delete;
  explicit TestConstruction(int) {
    defaultConstructs().fetch_add(1, std::memory_order_relaxed);
  }
  TestConstruction(TestConstruction&&) noexcept {
    moveConstructs().fetch_add(1, std::memory_order_relaxed);
  }
  TestConstruction(const TestConstruction&) {
    copyConstructs().fetch_add(1, std::memory_order_relaxed);
  }
  TestConstruction& operator=(const TestConstruction&) {
    copyAssigns().fetch_add(1, std::memory_order_relaxed);
    return *this;
  }
  TestConstruction& operator=(TestConstruction&&) {
    moveAssigns().fetch_add(1, std::memory_order_relaxed);
    return *this;
  }
  ~TestConstruction() {
    destructs().fetch_add(1, std::memory_order_relaxed);
  }

  static std::atomic<std::uint64_t>& defaultConstructs() {
    static auto&& atomic = std::atomic<std::uint64_t>{0};
    return atomic;
  }
  static std::atomic<std::uint64_t>& moveConstructs() {
    static auto&& atomic = std::atomic<std::uint64_t>{0};
    return atomic;
  }
  static std::atomic<std::uint64_t>& copyConstructs() {
    static auto&& atomic = std::atomic<std::uint64_t>{0};
    return atomic;
  }
  static std::atomic<std::uint64_t>& moveAssigns() {
    static auto&& atomic = std::atomic<std::uint64_t>{0};
    return atomic;
  }
  static std::atomic<std::uint64_t>& copyAssigns() {
    static auto&& atomic = std::atomic<std::uint64_t>{0};
    return atomic;
  }
  static std::atomic<std::uint64_t>& destructs() {
    static auto&& atomic = std::atomic<std::uint64_t>{0};
    return atomic;
  }

  static void reset() {
    defaultConstructs().store(0);
    moveConstructs().store(0);
    copyConstructs().store(0);
    copyAssigns().store(0);
    destructs().store(0);
  }
};
} // namespace

TEST(DistributedMutex, TestAppropriateDestructionAndConstructionWithCombine) {
  auto&& mutex = folly::DistributedMutex{};
  auto&& stop = std::atomic<bool>{false};

  // test the simple return path to make sure that in the absence of
  // contention, we get the right number of constructs and destructs
  mutex.lock_combine([]() { return TestConstruction{1}; });
  auto moves = TestConstruction::moveConstructs().load();
  auto defaults = TestConstruction::defaultConstructs().load();
  EXPECT_EQ(TestConstruction::defaultConstructs().load(), 1);
  EXPECT_TRUE(moves == 0 || moves == 1);
  EXPECT_EQ(TestConstruction::destructs().load(), moves + defaults);

  // loop and make sure we were able to test the path where the critical
  // section of the thread gets combined, and assert that we see the expected
  // number of constructions and destructions
  //
  // this implements a timed backoff to test the combined path, so we use the
  // smallest possible delay in tests
  auto thread = std::thread{[&]() {
    auto&& duration = std::chrono::milliseconds{10};
    while (!stop.load()) {
      TestConstruction::reset();
      auto&& ready = folly::Baton<>{};
      auto&& release = folly::Baton<>{};

      // make one thread start it's critical section, signal and wait for
      // another thread to enqueue, to test the
      auto innerThread = std::thread{[&]() {
        mutex.lock_combine([&]() {
          ready.post();
          release.wait();
          /* sleep override */
          std::this_thread::sleep_for(duration);
        });
      }};

      // wait for the thread to get in its critical section, then tell it to go
      ready.wait();
      release.post();
      mutex.lock_combine([&]() { return TestConstruction{1}; });

      innerThread.join();

      // at this point we should have only one default construct, either 3
      // or 4 move constructs the same number of destructions as
      // constructions
      auto innerDefaults = TestConstruction::defaultConstructs().load();
      auto innerMoves = TestConstruction::moveConstructs().load();
      auto destructs = TestConstruction::destructs().load();
      EXPECT_EQ(innerDefaults, 1);
      EXPECT_TRUE(innerMoves == 3 || innerMoves == 4 || innerMoves == 1);
      EXPECT_EQ(destructs, innerMoves + innerDefaults);
      EXPECT_EQ(TestConstruction::moveAssigns().load(), 0);
      EXPECT_EQ(TestConstruction::copyAssigns().load(), 0);

      // increase duration by 100ms each iteration
      duration = duration + 100ms;
    }
  }};

  /* sleep override */
  std::this_thread::sleep_for(std::chrono::seconds{kStressTestSeconds});
  stop.store(true);
  thread.join();
}

namespace {
template <template <typename> class Atom = std::atomic>
void concurrentLocksManyMutexes(int numThreads, std::chrono::seconds duration) {
  using DMutex = detail::distributed_mutex::DistributedMutex<Atom>;
  const auto&& kNumMutexes = 10;
  auto&& threads = std::vector<std::thread>{};
  auto&& mutexes = std::vector<DMutex>(kNumMutexes);
  auto&& barriers = std::vector<std::atomic<std::uint64_t>>(kNumMutexes);
  auto&& stop = std::atomic<bool>{false};

  for (auto i = 0; i < numThreads; ++i) {
    threads.push_back(DSched::thread([&] {
      auto&& total = std::atomic<std::uint64_t>{0};
      auto&& expected = std::uint64_t{0};

      for (auto j = 0; !stop.load(std::memory_order_relaxed); ++j) {
        auto& mutex = mutexes[j % kNumMutexes];
        auto& barrier = barriers[j % kNumMutexes];

        ++expected;
        auto result = mutex.lock_combine([&]() {
          EXPECT_EQ(barrier.fetch_add(1, std::memory_order_relaxed), 0);
          EXPECT_EQ(barrier.fetch_add(1, std::memory_order_relaxed), 1);
          std::this_thread::yield();
          SCOPE_EXIT {
            EXPECT_EQ(barrier.fetch_sub(1, std::memory_order_relaxed), 1);
          };
          EXPECT_EQ(barrier.fetch_sub(1, std::memory_order_relaxed), 2);
          return total.fetch_add(1, std::memory_order_relaxed);
        });
        EXPECT_EQ(result, expected - 1);
      }

      EXPECT_EQ(total.load(), expected);
    }));
  }

  /* sleep override */
  std::this_thread::sleep_for(duration);
  stop.store(true);
  for (auto& thread : threads) {
    DSched::join(thread);
  }
}
} // namespace

TEST(DistributedMutex, StressWithManyMutexesAlternatingTwoThreads) {
  concurrentLocksManyMutexes(2, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, StressWithManyMutexesAlternatingFourThreads) {
  concurrentLocksManyMutexes(4, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, StressWithManyMutexesAlternatingEightThreads) {
  concurrentLocksManyMutexes(8, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, StressWithManyMutexesAlternatingSixteenThreads) {
  concurrentLocksManyMutexes(16, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, StressWithManyMutexesAlternatingThirtyTwoThreads) {
  concurrentLocksManyMutexes(32, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, StressWithManyMutexesAlternatingSixtyFourThreads) {
  concurrentLocksManyMutexes(64, std::chrono::seconds{kStressTestSeconds});
}

namespace {
void concurrentLocksManyMutexesDeterministic(
    int threads,
    std::chrono::seconds t) {
  const auto kNumPasses = 3.0;
  const auto seconds = std::ceil(static_cast<double>(t.count()) / kNumPasses);
  const auto time = std::chrono::seconds{static_cast<std::uint64_t>(seconds)};

  for (auto pass = 0; pass < kNumPasses; ++pass) {
    auto&& schedule = DSched{DSched::uniform(pass)};
    concurrentLocksManyMutexes<test::DeterministicAtomic>(threads, time);
    static_cast<void>(schedule);
  }
}
} // namespace

TEST(DistributedMutex, DeterministicWithManyMutexesAlternatingTwoThreads) {
  concurrentLocksManyMutexesDeterministic(
      2, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, DeterministicWithManyMutexesAlternatingFourThreads) {
  concurrentLocksManyMutexesDeterministic(
      4, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, DeterministicWithManyMutexesAlternatingEightThreads) {
  concurrentLocksManyMutexesDeterministic(
      8, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, DeterministicWithManyMutexesAlternatingSixteenThreads) {
  concurrentLocksManyMutexesDeterministic(
      16, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, DeterministicWithManyMtxAlternatingThirtyTwoThreads) {
  concurrentLocksManyMutexesDeterministic(
      32, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, DeterministicWithManyMtxAlternatingSixtyFourThreads) {
  concurrentLocksManyMutexesDeterministic(
      64, std::chrono::seconds{kStressTestSeconds});
}

namespace {
class ExceptionWithConstructionTrack : public std::exception {
 public:
  explicit ExceptionWithConstructionTrack(int id)
      : id_{folly::to<std::string>(id)}, constructionTrack_{id} {}

  const char* what() const noexcept override {
    return id_.c_str();
  }

 private:
  std::string id_;
  TestConstruction constructionTrack_;
};
} // namespace

TEST(DistributedMutex, TestExceptionPropagationUncontended) {
  TestConstruction::reset();
  auto&& mutex = folly::DistributedMutex{};

  auto&& thread = std::thread{[&]() {
    try {
      mutex.lock_combine([&]() { throw ExceptionWithConstructionTrack{46}; });
    } catch (std::exception& exc) {
      auto integer = folly::to<std::uint64_t>(exc.what());
      EXPECT_EQ(integer, 46);
      EXPECT_GT(TestConstruction::defaultConstructs(), 0);
    }

    EXPECT_EQ(
        TestConstruction::defaultConstructs(), TestConstruction::destructs());
  }};

  thread.join();
}

namespace {
template <template <typename> class Atom = std::atomic>
void concurrentExceptionPropagationStress(
    int numThreads,
    std::chrono::milliseconds t) {
  // this test passes normally and under recent or Clang TSAN, but inexplicably
  // TSAN-aborts under some older non-Clang TSAN versions
  if (folly::kIsSanitizeThread && !folly::kIsClang) {
    return;
  }

  TestConstruction::reset();
  auto&& mutex = detail::distributed_mutex::DistributedMutex<Atom>{};
  auto&& threads = std::vector<std::thread>{};
  auto&& stop = std::atomic<bool>{false};
  auto&& barrier = std::atomic<std::uint64_t>{0};

  for (auto i = 0; i < numThreads; ++i) {
    threads.push_back(DSched::thread([&]() {
      for (auto j = 0; !stop.load(); ++j) {
        auto value = int{0};
        try {
          value = mutex.lock_combine([&]() {
            EXPECT_EQ(barrier.fetch_add(1, std::memory_order_relaxed), 0);
            EXPECT_EQ(barrier.fetch_add(1, std::memory_order_relaxed), 1);
            std::this_thread::yield();
            SCOPE_EXIT {
              EXPECT_EQ(barrier.fetch_sub(1, std::memory_order_relaxed), 1);
            };
            EXPECT_EQ(barrier.fetch_sub(1, std::memory_order_relaxed), 2);

            // we only throw an exception once every 3 times
            if (!(j % 3)) {
              throw ExceptionWithConstructionTrack{j};
            }

            return j;
          });
        } catch (std::exception& exc) {
          value = folly::to<int>(exc.what());
        }

        EXPECT_EQ(value, j);
      }
    }));
  }

  /* sleep override */
  std::this_thread::sleep_for(t);
  stop.store(true);
  for (auto& thread : threads) {
    DSched::join(thread);
  }
}
} // namespace

TEST(DistributedMutex, TestExceptionPropagationStressTwoThreads) {
  concurrentExceptionPropagationStress(
      2, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, TestExceptionPropagationStressFourThreads) {
  concurrentExceptionPropagationStress(
      4, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, TestExceptionPropagationStressEightThreads) {
  concurrentExceptionPropagationStress(
      8, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, TestExceptionPropagationStressSixteenThreads) {
  concurrentExceptionPropagationStress(
      16, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, TestExceptionPropagationStressThirtyTwoThreads) {
  concurrentExceptionPropagationStress(
      32, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, TestExceptionPropagationStressSixtyFourThreads) {
  concurrentExceptionPropagationStress(
      64, std::chrono::seconds{kStressTestSeconds});
}

namespace {
void concurrentExceptionPropagationDeterministic(
    int threads,
    std::chrono::seconds t) {
  const auto kNumPasses = 3.0;
  const auto seconds = std::ceil(static_cast<double>(t.count()) / kNumPasses);
  const auto time = std::chrono::seconds{static_cast<std::uint64_t>(seconds)};

  for (auto pass = 0; pass < kNumPasses; ++pass) {
    auto&& schedule = DSched{DSched::uniform(pass)};
    concurrentExceptionPropagationStress<test::DeterministicAtomic>(
        threads, time);
    static_cast<void>(schedule);
  }
}
} // namespace

TEST(DistributedMutex, TestExceptionPropagationDeterministicTwoThreads) {
  concurrentExceptionPropagationDeterministic(
      2, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, TestExceptionPropagationDeterministicFourThreads) {
  concurrentExceptionPropagationDeterministic(
      4, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, TestExceptionPropagationDeterministicEightThreads) {
  concurrentExceptionPropagationDeterministic(
      8, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, TestExceptionPropagationDeterministicSixteenThreads) {
  concurrentExceptionPropagationDeterministic(
      16, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, TestExceptionPropagationDeterministicThirtyTwoThreads) {
  concurrentExceptionPropagationDeterministic(
      32, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, TestExceptionPropagationDeterministicSixtyFourThreads) {
  concurrentExceptionPropagationDeterministic(
      64, std::chrono::seconds{kStressTestSeconds});
}

namespace {
std::array<std::uint64_t, 8> makeMonotonicArray(int start) {
  auto array = std::array<std::uint64_t, 8>{};
  folly::for_each(array, [&](auto& element) { element = start++; });
  return array;
}

template <template <typename> class Atom = std::atomic>
void concurrentBigValueReturnStress(
    int numThreads,
    std::chrono::milliseconds t) {
  auto&& mutex = detail::distributed_mutex::DistributedMutex<Atom>{};
  auto&& threads = std::vector<std::thread>{};
  auto&& stop = std::atomic<bool>{false};
  auto&& barrier = std::atomic<std::uint64_t>{0};

  for (auto i = 0; i < numThreads; ++i) {
    threads.push_back(DSched::thread([&]() {
      auto&& value = std::atomic<std::uint64_t>{0};

      for (auto j = 0; !stop.load(); ++j) {
        auto returned = mutex.lock_combine([&]() {
          EXPECT_EQ(barrier.fetch_add(1, std::memory_order_relaxed), 0);
          EXPECT_EQ(barrier.fetch_add(1, std::memory_order_relaxed), 1);
          std::this_thread::yield();
          // return an entire cacheline worth of data
          auto current = value.fetch_add(1, std::memory_order_relaxed);
          SCOPE_EXIT {
            EXPECT_EQ(barrier.fetch_sub(1, std::memory_order_relaxed), 1);
          };
          EXPECT_EQ(barrier.fetch_sub(1, std::memory_order_relaxed), 2);
          return makeMonotonicArray(current);
        });

        auto expected = value.load() - 1;
        folly::for_each(
            returned, [&](auto& element) { EXPECT_EQ(element, expected++); });
      }
    }));
  }

  /* sleep override */
  std::this_thread::sleep_for(t);
  stop.store(true);
  for (auto& thread : threads) {
    DSched::join(thread);
  }
}
} // namespace

TEST(DistributedMutex, StressBigValueReturnTwoThreads) {
  concurrentBigValueReturnStress(2, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, StressBigValueReturnFourThreads) {
  concurrentBigValueReturnStress(4, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, StressBigValueReturnEightThreads) {
  concurrentBigValueReturnStress(8, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, StressBigValueReturnSixteenThreads) {
  concurrentBigValueReturnStress(16, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, StressBigValueReturnThirtyTwoThreads) {
  concurrentBigValueReturnStress(32, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, StressBigValueReturnSixtyFourThreads) {
  concurrentBigValueReturnStress(64, std::chrono::seconds{kStressTestSeconds});
}

namespace {
void concurrentBigValueReturnDeterministic(
    int threads,
    std::chrono::seconds t) {
  const auto kNumPasses = 3.0;
  const auto seconds = std::ceil(static_cast<double>(t.count()) / kNumPasses);
  const auto time = std::chrono::seconds{static_cast<std::uint64_t>(seconds)};

  for (auto pass = 0; pass < kNumPasses; ++pass) {
    auto&& schedule = DSched{DSched::uniform(pass)};
    concurrentBigValueReturnStress<test::DeterministicAtomic>(threads, time);
    static_cast<void>(schedule);
  }
}
} // namespace

TEST(DistributedMutex, DeterministicBigValueReturnTwoThreads) {
  concurrentBigValueReturnDeterministic(
      2, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, DeterministicBigValueReturnFourThreads) {
  concurrentBigValueReturnDeterministic(
      4, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, DeterministicBigValueReturnEightThreads) {
  concurrentBigValueReturnDeterministic(
      8, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, DeterministicBigValueReturnSixteenThreads) {
  concurrentBigValueReturnDeterministic(
      16, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, DeterministicBigValueReturnThirtyTwoThreads) {
  concurrentBigValueReturnDeterministic(
      32, std::chrono::seconds{kStressTestSeconds});
}
TEST(DistributedMutex, DeterministicBigValueReturnSixtyFourThreads) {
  concurrentBigValueReturnDeterministic(
      64, std::chrono::seconds{kStressTestSeconds});
}
} // namespace folly
