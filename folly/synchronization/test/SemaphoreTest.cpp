/*
 * Copyright 2019-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <folly/synchronization/test/Semaphore.h>

#include <array>
#include <thread>
#include <vector>

#include <folly/portability/GTest.h>
#include <folly/synchronization/test/Barrier.h>

using namespace folly::test;

namespace {

class WaitForAll {
 public:
  explicit WaitForAll(size_t rem) : remaining(rem) {}

  void post() {
    std::unique_lock<std::mutex> l{m};
    assert(remaining);
    if (!--remaining) {
      c.notify_one();
    }
  }
  void wait() {
    std::unique_lock<std::mutex> l{m};
    c.wait(l, [&] { return !remaining; });
  }

 private:
  size_t remaining;
  std::mutex m;
  std::condition_variable c;
};

template <SemaphoreWakePolicy WakePolicy>
auto wake_policy(Semaphore<WakePolicy> const&) {
  return WakePolicy;
}

template <typename Sem>
void test_basic() {
  Sem sem;
  EXPECT_FALSE(sem.try_wait());
  sem.post();
  EXPECT_TRUE(sem.try_wait());
  sem.post();
  sem.wait();
}

template <typename Sem>
void test_wake_policy() {
  constexpr auto const nthreads = 16ull;
  constexpr auto const rounds = 1ull << 4;

  Sem sem;
  std::array<std::thread, nthreads> threads;
  for (auto i = 0ull; i < rounds; ++i) {
    std::vector<uint64_t> wait_seq;
    std::vector<uint64_t> wake_seq;
    WaitForAll ready(nthreads); // first nthreads waits, then nthreads posts
    for (auto thi = 0ull; thi < nthreads; ++thi) {
      threads[thi] = std::thread([&, thi] {
        sem.wait(
            [&, thi] { wait_seq.push_back(thi), ready.post(); },
            [&, thi] { wake_seq.push_back(thi); });
      });
    }
    ready.wait(); // first nthreads waits, then nthreads posts
    for (auto thi = 0ull; thi < nthreads; ++thi) {
      sem.post();
    }
    for (auto thi = 0ull; thi < nthreads; ++thi) {
      threads[thi].join();
    }
    EXPECT_EQ(nthreads, wait_seq.size());
    EXPECT_EQ(nthreads, wake_seq.size());
    switch (wake_policy(sem)) {
      case SemaphoreWakePolicy::Fifo:
        break;
      case SemaphoreWakePolicy::Lifo:
        std::reverse(wake_seq.begin(), wake_seq.end());
        break;
    }
    EXPECT_EQ(wait_seq, wake_seq);
  }
}

template <typename Sem>
void test_multi_ping_pong() {
  constexpr auto const nthreads = 4ull;
  constexpr auto const iters = 1ull << 12;

  Sem sem;
  std::array<std::thread, nthreads> threads;
  size_t waits_before = 0;
  size_t waits_after = 0;
  size_t posts = 0;

  for (auto& th : threads) {
    th = std::thread([&] {
      for (auto i = 0ull; i < iters; ++i) {
        sem.wait([&] { ++waits_before; }, [&] { ++waits_after; });
        sem.post([&] { ++posts; });
      }
    });
  }

  sem.post(); // start the flood

  for (auto& thr : threads) {
    thr.join();
  }

  sem.wait();
  EXPECT_FALSE(sem.try_wait());
  EXPECT_EQ(iters * nthreads, waits_before);
  EXPECT_EQ(iters * nthreads, waits_after);
  EXPECT_EQ(iters * nthreads, posts);
}

template <typename Sem>
void test_concurrent_split_waiters_posters() {
  constexpr auto const nthreads = 4ull;
  constexpr auto const iters = 1ull << 12;

  Sem sem;
  Barrier barrier(nthreads * 2);
  std::array<std::thread, nthreads> posters;
  std::array<std::thread, nthreads> waiters;

  for (auto& th : posters) {
    th = std::thread([&] {
      barrier.wait();
      for (auto i = 0ull; i < iters; ++i) {
        if (i % (iters >> 4) == 0) {
          std::this_thread::yield();
        }
        sem.post();
      }
    });
  }
  for (auto& th : waiters) {
    th = std::thread([&] {
      barrier.wait();
      for (auto i = 0ull; i < iters; ++i) {
        sem.wait();
      }
    });
  }

  for (auto& th : posters) {
    th.join();
  }
  for (auto& th : waiters) {
    th.join();
  }

  EXPECT_FALSE(sem.try_wait());
}

} // namespace

class FifoSemaphoreTest : public testing::Test {};

TEST_F(FifoSemaphoreTest, basic) {
  test_basic<FifoSemaphore>();
}

TEST_F(FifoSemaphoreTest, wake_policy) {
  test_wake_policy<FifoSemaphore>();
}

TEST_F(FifoSemaphoreTest, multi_ping_pong) {
  test_multi_ping_pong<FifoSemaphore>();
}

TEST_F(FifoSemaphoreTest, concurrent_split_waiters_posters) {
  test_concurrent_split_waiters_posters<FifoSemaphore>();
}

class LifoSemaphoreTest : public testing::Test {};

TEST_F(LifoSemaphoreTest, basic) {
  test_basic<LifoSemaphore>();
}

TEST_F(LifoSemaphoreTest, wake_policy) {
  test_wake_policy<LifoSemaphore>();
}

TEST_F(LifoSemaphoreTest, multi_ping_pong) {
  test_multi_ping_pong<LifoSemaphore>();
}

TEST_F(LifoSemaphoreTest, concurrent_split_waiters_posters) {
  test_concurrent_split_waiters_posters<LifoSemaphore>();
}
