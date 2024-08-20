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

#include <folly/experimental/channels/detail/AtomicQueue.h>

#include <folly/portability/GTest.h>

#include <folly/synchronization/Baton.h>

namespace folly {
namespace channels {
namespace detail {

static int* getConsumerParam() {
  return reinterpret_cast<int*>(1);
}

TEST(AtomicQueueTest, Basic) {
  folly::Baton<> producerBaton;
  folly::Baton<> consumerBaton;

  struct Consumer {
    void consume(int* consumerParam) {
      EXPECT_EQ(consumerParam, getConsumerParam());
      baton.post();
    }
    void canceled(int*) { ADD_FAILURE() << "canceled() shouldn't be called"; }
    folly::Baton<> baton;
  };
  AtomicQueue<Consumer, int> atomicQueue;
  Consumer consumer;

  std::thread producerThread([&] {
    producerBaton.wait();
    producerBaton.reset();

    atomicQueue.push(1, getConsumerParam());

    producerBaton.wait();
    producerBaton.reset();

    atomicQueue.push(2, getConsumerParam());
    atomicQueue.push(3, getConsumerParam());
    consumerBaton.post();
  });

  EXPECT_TRUE(atomicQueue.wait(&consumer, getConsumerParam()));
  producerBaton.post();
  consumer.baton.wait();
  consumer.baton.reset();

  {
    auto q = atomicQueue.getMessages(getConsumerParam());
    EXPECT_FALSE(q.empty());
    EXPECT_EQ(1, q.front());
    q.pop();
    EXPECT_TRUE(q.empty());
  }

  producerBaton.post();
  consumerBaton.wait();
  consumerBaton.reset();

  EXPECT_FALSE(atomicQueue.wait(&consumer, getConsumerParam()));
  {
    auto q = atomicQueue.getMessages(getConsumerParam());
    EXPECT_FALSE(q.empty());
    EXPECT_EQ(2, q.front());
    q.pop();
    EXPECT_FALSE(q.empty());
    EXPECT_EQ(3, q.front());
    q.pop();
    EXPECT_TRUE(q.empty());
  }

  EXPECT_TRUE(atomicQueue.wait(&consumer, getConsumerParam()));
  EXPECT_EQ(atomicQueue.cancelCallback(), &consumer);

  EXPECT_TRUE(atomicQueue.wait(&consumer, getConsumerParam()));
  EXPECT_EQ(atomicQueue.cancelCallback(), &consumer);

  EXPECT_EQ(atomicQueue.cancelCallback(), nullptr);

  producerThread.join();
}

TEST(AtomicQueueTest, Canceled) {
  struct Consumer {
    void consume(int*) { ADD_FAILURE() << "consume() shouldn't be called"; }
    void canceled(int* consumerParam) {
      EXPECT_EQ(consumerParam, getConsumerParam());
      canceledCalled = true;
    }
    bool canceledCalled{false};
  };
  AtomicQueue<Consumer, int> atomicQueue;
  Consumer consumer;

  EXPECT_TRUE(atomicQueue.wait(&consumer, getConsumerParam()));
  atomicQueue.close(getConsumerParam());
  EXPECT_TRUE(consumer.canceledCalled);
  EXPECT_TRUE(atomicQueue.isClosed());

  EXPECT_TRUE(atomicQueue.getMessages(getConsumerParam()).empty());
  EXPECT_TRUE(atomicQueue.isClosed());

  atomicQueue.push(42, getConsumerParam());

  EXPECT_TRUE(atomicQueue.getMessages(getConsumerParam()).empty());
  EXPECT_TRUE(atomicQueue.isClosed());
}

TEST(AtomicQueueTest, Stress) {
  struct Consumer {
    void consume(int* consumerParam) {
      EXPECT_EQ(consumerParam, getConsumerParam());
      baton.post();
    }
    void canceled(int*) { ADD_FAILURE() << "canceled() shouldn't be called"; }
    folly::Baton<> baton;
  };
  AtomicQueue<Consumer, int> atomicQueue;
  auto getNext = [&atomicQueue, queue = Queue<int>()]() mutable {
    Consumer consumer;
    if (queue.empty()) {
      if (atomicQueue.wait(&consumer, getConsumerParam())) {
        consumer.baton.wait();
      }
      queue = atomicQueue.getMessages(getConsumerParam());
      EXPECT_FALSE(queue.empty());
    }
    auto next = queue.front();
    queue.pop();
    return next;
  };

  constexpr ssize_t kNumIters = 100000;
  constexpr ssize_t kSynchronizeEvery = 1000;

  std::atomic<ssize_t> producerIndex{0};
  std::atomic<ssize_t> consumerIndex{0};

  std::thread producerThread([&] {
    for (producerIndex = 1; producerIndex <= kNumIters; ++producerIndex) {
      atomicQueue.push(producerIndex, getConsumerParam());

      if (producerIndex % kSynchronizeEvery == 0) {
        while (producerIndex > consumerIndex.load(std::memory_order_relaxed)) {
          std::this_thread::yield();
        }
      }
    }
  });

  for (consumerIndex = 1; consumerIndex <= kNumIters; ++consumerIndex) {
    EXPECT_EQ(consumerIndex, getNext());

    if (consumerIndex % kSynchronizeEvery == 0) {
      while (consumerIndex > producerIndex.load(std::memory_order_relaxed)) {
        std::this_thread::yield();
      }
    }
  }

  producerThread.join();
}

} // namespace detail
} // namespace channels
} // namespace folly
