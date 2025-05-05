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

#include <folly/experimental/coro/BoundedQueue.h>

#include <chrono>
#include <optional>
#include <string>
#include <thread>

#include <folly/CancellationToken.h>
#include <folly/Portability.h>
#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/Collect.h>
#include <folly/experimental/coro/GtestHelpers.h>
#include <folly/experimental/coro/Sleep.h>
#include <folly/portability/GTest.h>
#if FOLLY_HAS_COROUTINES

namespace {
struct SlowMover {
  explicit SlowMover(bool slow = false) : slow(slow) {}
  SlowMover(SlowMover&& other) noexcept { *this = std::move(other); }
  SlowMover& operator=(SlowMover&& other) noexcept {
    slow = other.slow;
    if (slow) {
      /* sleep override */ std::this_thread::sleep_for(
          std::chrono::milliseconds(50));
    }
    return *this;
  }

  bool slow;
};
} // namespace

CO_TEST(BoundedQueueTest, EnqueueDeque) {
  folly::coro::BoundedQueue<std::string, true, true> queue(100);
  constexpr auto val = "a string";
  std::string val1 = val;
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(queue.size(), 0);

  co_await queue.enqueue(val1);
  EXPECT_FALSE(queue.empty());
  co_await queue.enqueue(std::move(val1));
  EXPECT_EQ(queue.size(), 2);

  for (int i = 0; i < 2; ++i) {
    auto val2 = co_await queue.dequeue();
    EXPECT_EQ(val2, val);
  }
  EXPECT_TRUE(queue.empty());
}

CO_TEST(BoundedQueueTest, DequeueWhileBlocking) {
  folly::coro::BoundedQueue<int> queue(5);
  folly::ManualExecutor ex;

  auto fut = queue.dequeue().scheduleOn(&ex).start();
  ex.drain();
  EXPECT_FALSE(fut.isReady());
  co_await queue.enqueue(0);

  ex.drain();
  EXPECT_TRUE(fut.isReady());
  EXPECT_EQ(std::move(fut).get(), 0);
}

CO_TEST(BoundedQueueTest, EnqueueDequeMultiProducer) {
  folly::coro::BoundedQueue<int, false, true> queue(5);
  std::atomic<int> i = 0;

  std::vector<std::thread> enqueuers;
  for (int n = 0; n < 5; ++n) {
    enqueuers.emplace_back([&] {
      while (true) {
        int next = i++;
        if (next >= 100) {
          break;
        }
        folly::coro::blockingWait(
            [&, next]() mutable -> folly::coro::Task<void> {
              co_await queue.enqueue(std::move(next));
            }());
      }
    });
  }
  for (int n = 0; n < 100; ++n) {
    co_await queue.dequeue();
  }

  EXPECT_TRUE(queue.empty());

  for (int n = 0; n < 5; ++n) {
    enqueuers[n].join();
  }
}

CO_TEST(BoundedQueueTest, EnqueueDequeMultiConsumer) {
  folly::coro::BoundedQueue<int, true, false> queue(10);
  std::atomic<int> seen = 0;

  std::vector<std::thread> dequeuers;
  for (int n = 0; n < 5; ++n) {
    dequeuers.emplace_back([&] {
      folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
        while (++seen <= 100) {
          co_await queue.dequeue();
        }
      }());
    });
  }

  for (int n = 0; n < 100; ++n) {
    co_await queue.enqueue(std::move(n));
  }
  for (int n = 0; n < 5; ++n) {
    dequeuers[n].join();
  }
  EXPECT_TRUE(queue.empty());
}

CO_TEST(BoundedQueueTest, EnqueueDequeMPMCWithSingleSlot) {
  folly::coro::BoundedQueue<int, false, false> queue(1);
  std::atomic<int> seen = 0, i = 0;

  std::vector<std::thread> enqueuers;
  for (int n = 0; n < 5; ++n) {
    enqueuers.emplace_back([&] {
      folly::coro::blockingWait([&]() mutable -> folly::coro::Task<void> {
        while (true) {
          int next = i++;
          if (next >= 100) {
            break;
          }
          co_await queue.enqueue(std::move(next));
        }
      }());
    });
  }

  std::vector<std::thread> dequeuers;
  for (int n = 0; n < 5; ++n) {
    dequeuers.emplace_back([&] {
      folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
        while (++seen <= 100) {
          co_await queue.dequeue();
        }
      }());
    });
  }

  for (int n = 0; n < 5; ++n) {
    enqueuers[n].join();
  }
  for (int n = 0; n < 5; ++n) {
    dequeuers[n].join();
  }
  EXPECT_TRUE(queue.empty());

  co_return;
}

CO_TEST(
    BoundedQueueTest, CancelledDequeueCompletesNormallyIfAnItemIsAvailable) {
  folly::coro::BoundedQueue<int> queue(10);
  folly::CancellationSource cancelSource;
  cancelSource.requestCancellation();

  co_await queue.enqueue(123);

  int result = co_await folly::coro::co_withCancellation(
      cancelSource.getToken(), queue.dequeue());
  EXPECT_EQ(123, result);
}

CO_TEST(BoundedQueueTest, EnqueueWait) {
  folly::coro::BoundedQueue<int> queue(2);
  co_await folly::coro::collectAll(
      [&]() -> folly::coro::Task<void> {
        for (int i = 0; i < 100; i++) {
          auto val = i;
          co_await queue.enqueue(std::move(val));
        }
      }(),
      [&]() -> folly::coro::Task<void> {
        for (int i = 0; i < 100; i++) {
          int val = co_await queue.dequeue();
          EXPECT_EQ(val, i);
        }
      }());
}

CO_TEST(BoundedQueueTest, DequeueWait) {
  folly::coro::BoundedQueue<int> queue(2);
  co_await folly::coro::collectAll(
      [&]() -> folly::coro::Task<void> {
        for (int i = 0; i < 100; i++) {
          auto val = i;
          co_await queue.enqueue(std::move(val));
        }
      }(),
      [&]() -> folly::coro::Task<void> {
        for (int i = 0; i < 100; i++) {
          int val = co_await queue.dequeue();
          EXPECT_EQ(val, i);
        }
      }());
}

CO_TEST(BoundedQueueTest, TryEnqueue) {
  folly::coro::BoundedQueue<int> queue(2);

  EXPECT_TRUE(queue.try_enqueue(1));
  EXPECT_TRUE(queue.try_enqueue(1));
  EXPECT_FALSE(queue.try_enqueue(1));
  co_await queue.dequeue();
  EXPECT_TRUE(queue.try_enqueue(1));
}

CO_TEST(BoundedQueueTest, TryDequeue) {
  folly::coro::BoundedQueue<int> queue(2);

  EXPECT_FALSE(queue.try_dequeue().has_value());
  co_await queue.enqueue(1);
  EXPECT_TRUE(queue.try_dequeue().has_value());
}

TEST(BoundedQueueTest, UnorderedEnqueueCompletion) {
  // Use optional to verify we're not accidentally dequeueing
  // default-constructed values.
  folly::coro::BoundedQueue<std::optional<SlowMover>> queue(1024);
  std::atomic<int> turn = 0;

  std::thread consumer([&] {
    ++turn;
    for (size_t i = 0; i < 2; ++i) {
      ASSERT_TRUE(folly::coro::blockingWait(queue.dequeue()).has_value());
    }
  });

  // producer2 will frequently initiate the enqueue after producer1 (thus
  // acquiring a larger ticket) but complete the move after it. The consumer
  // thus needs to block until the head-of-line item is available.
  std::thread producer1([&] {
    ++turn;
    while (turn < 3) {
    }
    ++turn;
    ASSERT_TRUE(queue.try_enqueue(SlowMover(true)));
  });
  std::thread producer2([&] {
    ++turn;
    while (turn < 4) {
    }
    /* sleep override */ std::this_thread::sleep_for(
        std::chrono::milliseconds(1));
    ASSERT_TRUE(queue.try_enqueue(SlowMover(false)));
  });

  producer1.join();
  producer2.join();
  consumer.join();
}

TEST(BoundedQueueTest, UnorderedDequeueCompletion) {
  // Use optional to verify we're not accidentally dequeueing
  // default-constructed values.
  folly::coro::BoundedQueue<std::optional<SlowMover>> queue(2);

  ASSERT_TRUE(queue.try_enqueue(SlowMover(true)));
  ASSERT_TRUE(queue.try_enqueue(SlowMover(false)));

  std::vector<std::thread> consumers;
  for (size_t i = 0; i < 3; ++i) {
    consumers.emplace_back([&]() {
      ASSERT_TRUE(folly::coro::blockingWait(queue.dequeue()).has_value());
    });
  }

  // The producer will get the ticket for the slow moving slot which will still
  // be in the process of dequeuing, so the producer needs to block until it
  // finishes and the slot becomes available.
  std::thread producer(
      [&] { folly::coro::blockingWait(queue.enqueue(SlowMover(false))); });

  producer.join();
  for (auto& consumer : consumers) {
    consumer.join();
  }
}

#endif
