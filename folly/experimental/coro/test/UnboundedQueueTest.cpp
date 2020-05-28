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

#include <folly/Portability.h>

#if FOLLY_HAS_COROUTINES

#include <folly/CancellationToken.h>
#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/Collect.h>
#include <folly/experimental/coro/UnboundedQueue.h>

#include <folly/portability/GTest.h>

#include <string>
#include <thread>

TEST(UnboundedQueueTest, EnqueueDeque) {
  folly::coro::UnboundedQueue<std::string, true, true> queue;
  constexpr auto val = "a string";
  std::string val1 = val;
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(queue.size(), 0);
  queue.enqueue(val1);
  EXPECT_FALSE(queue.empty());
  queue.enqueue(std::move(val1));
  EXPECT_EQ(queue.size(), 2);
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    for (int i = 0; i < 2; ++i) {
      auto val2 = co_await queue.dequeue();
      EXPECT_EQ(val2, val);
    }
    EXPECT_TRUE(queue.empty());
  }());
}

TEST(UnboundedQueueTest, DequeueWhileBlocking) {
  folly::coro::UnboundedQueue<int> queue;
  folly::ManualExecutor ex;

  auto fut = queue.dequeue().scheduleOn(&ex).start();
  ex.drain();
  EXPECT_FALSE(fut.isReady());

  queue.enqueue(0);
  ex.drain();
  EXPECT_TRUE(fut.isReady());
  EXPECT_EQ(std::move(fut).get(), 0);
}

TEST(UnboundedQueueTest, EnqueueDequeMultiProducer) {
  folly::coro::UnboundedQueue<int, false, true> queue;
  std::atomic<int> i = 0;

  std::vector<std::thread> enqueuers;
  for (int n = 0; n < 5; ++n) {
    enqueuers.emplace_back([&] {
      while (true) {
        int next = i++;
        if (next >= 100) {
          break;
        }
        queue.enqueue(next);
      }
    });
  }

  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    for (int n = 0; n < 100; ++n) {
      co_await queue.dequeue();
    }
  }());
  EXPECT_TRUE(queue.empty());

  for (int n = 0; n < 5; ++n) {
    enqueuers[n].join();
  }
}

TEST(UnboundedQueueTest, EnqueueDequeMultiConsumer) {
  folly::coro::UnboundedQueue<int, true, false> queue;
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
    queue.enqueue(n);
  }
  for (int n = 0; n < 5; ++n) {
    dequeuers[n].join();
  }
  EXPECT_TRUE(queue.empty());
}

TEST(UnboundedQueueTest, EnqueueDequeMPMC) {
  folly::coro::UnboundedQueue<int, false, false> queue;
  std::atomic<int> seen = 0, i = 0;

  std::vector<std::thread> enqueuers;
  for (int n = 0; n < 5; ++n) {
    enqueuers.emplace_back([&] {
      while (true) {
        int next = i++;
        if (next >= 100) {
          break;
        }
        queue.enqueue(next);
      }
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
}

TEST(UnboundedQueueTest, CancelledDequeueThrowsOperationCancelled) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    // Cancellation currently only supported on SingleConsumer variants of
    // UnboundedQueue.
    folly::coro::UnboundedQueue<int> queue;
    folly::CancellationSource cancelSource;

    co_await folly::coro::collectAll(
        [&]() -> folly::coro::Task<void> {
          EXPECT_THROW(
              (co_await folly::coro::co_withCancellation(
                  cancelSource.getToken(), queue.dequeue())),
              folly::OperationCancelled);
        }(),
        [&]() -> folly::coro::Task<void> {
          co_await folly::coro::co_reschedule_on_current_executor;
          co_await folly::coro::co_reschedule_on_current_executor;
          cancelSource.requestCancellation();
        }());
  }());
}

TEST(UnboundedQueueTest, CancelledDequeueCompletesNormallyIfAnItemIsAvailable) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    // Cancellation currently only supported on SingleConsumer variants of
    // UnboundedQueue.
    folly::coro::UnboundedQueue<int> queue;
    folly::CancellationSource cancelSource;
    cancelSource.requestCancellation();

    queue.enqueue(123);

    int result = co_await folly::coro::co_withCancellation(
        cancelSource.getToken(), queue.dequeue());
    EXPECT_EQ(123, result);
  }());
}

#endif
