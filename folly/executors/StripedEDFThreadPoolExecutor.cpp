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

#include <folly/executors/StripedEDFThreadPoolExecutor.h>

#include <optional>

#include <glog/logging.h>
#include <folly/concurrency/CacheLocality.h>
#include <folly/container/IntrusiveHeap.h>
#include <folly/synchronization/DistributedMutex.h>
#include <folly/synchronization/StripedThrottledLifoSem.h>

namespace folly {

namespace {

template <class T>
class EDFPriorityQueue {
 public:
  EDFPriorityQueue() = default;

  ~EDFPriorityQueue() {
    while (!heap_.empty()) {
      delete heap_.pop();
    }
  }

  void enqueue(T&& item, uint64_t deadline) {
    std::unique_ptr<Node> node{new Node{std::move(item), deadline}};
    mutex_.lock_combine([&] { heap_.push(node.release()); });
  }

  T dequeue() {
    std::unique_ptr<Node> node;
    mutex_.lock_combine([&] { node.reset(heap_.pop()); });
    CHECK(node);
    return std::move(node->item);
  }

 private:
  struct Node : IntrusiveHeapNode<> {
    Node(T&& it, uint64_t d) : item(std::move(it)), deadline(d) {}

    T item;
    uint64_t deadline;

    friend bool operator<(const Node& lhs, const Node& rhs) {
      return lhs.deadline > rhs.deadline;
    }
  };

  EDFPriorityQueue(const EDFPriorityQueue&) = delete;
  EDFPriorityQueue(EDFPriorityQueue&&) = delete;
  EDFPriorityQueue& operator=(const EDFPriorityQueue&) = delete;
  EDFPriorityQueue& operator=(EDFPriorityQueue&&) = delete;

  DistributedMutex mutex_;
  IntrusiveHeap<Node> heap_;
};

template <class T>
class BlockingQueueWithDeadline : public BlockingQueue<T> {
 public:
  virtual BlockingQueueAddResult addWithDeadline(T item, uint64_t deadline) = 0;
};

template <class T>
class StripedEDFPriorityBlockingQueue final
    : public BlockingQueueWithDeadline<T> {
 public:
  explicit StripedEDFPriorityBlockingQueue(
      const ThrottledLifoSem::Options& options)
      : sem_(LLCAccessSpreader::get().numStripes(), std::tuple{}, options) {
    StripedThrottledLifoSemBalancer::subscribe(sem_);
  }

  ~StripedEDFPriorityBlockingQueue() override {
    StripedThrottledLifoSemBalancer::unsubscribe(sem_);
  }

  BlockingQueueAddResult addWithDeadline(T item, uint64_t deadline) override {
    const auto stripeIdx = getStripeIdx();
    sem_.payload(stripeIdx).enqueue(std::move(item), deadline);
    return sem_.post(stripeIdx);
  }

  BlockingQueueAddResult add(T item) override {
    return addWithDeadline(
        std::move(item), StripedEDFThreadPoolExecutor::kLatestDeadline);
  }

  T take() override {
    const auto stripeIdx = getStripeIdx();
    auto foundStripeIdx = sem_.wait(stripeIdx);
    return sem_.payload(foundStripeIdx).dequeue();
  }

  folly::Optional<T> try_take_for(std::chrono::milliseconds time) override {
    const auto stripeIdx = getStripeIdx();
    if (auto foundStripeIdx = sem_.try_wait_for(stripeIdx, time)) {
      return sem_.payload(*foundStripeIdx).dequeue();
    }
    return none;
  }

  size_t size() override { return sem_.valueGuess(); }

 private:
  static size_t getStripeIdx() { return LLCAccessSpreader::get().current(); }

  StripedThrottledLifoSem<EDFPriorityQueue<T>> sem_;
};

// Specialized implementation for the numStripes == 1 case, to avoid the
// StripedThrottledLifoSem overhead when we don't need it.
template <class T>
class EDFPriorityBlockingQueue final : public BlockingQueueWithDeadline<T> {
 public:
  explicit EDFPriorityBlockingQueue(const ThrottledLifoSem::Options& options)
      : sem_(options) {}

  BlockingQueueAddResult addWithDeadline(T item, uint64_t deadline) override {
    pq_.enqueue(std::move(item), deadline);
    return sem_.post();
  }

  BlockingQueueAddResult add(T item) override {
    return addWithDeadline(
        std::move(item), StripedEDFThreadPoolExecutor::kLatestDeadline);
  }

  T take() override {
    sem_.wait();
    return pq_.dequeue();
  }

  folly::Optional<T> try_take_for(std::chrono::milliseconds time) override {
    if (sem_.try_wait_for(time)) {
      return pq_.dequeue();
    }
    return none;
  }

  size_t size() override { return sem_.valueGuess(); }

 private:
  EDFPriorityQueue<T> pq_;
  ThrottledLifoSem sem_;
};

std::unique_ptr<BlockingQueue<CPUThreadPoolExecutor::CPUTask>> makeQueue(
    const StripedEDFThreadPoolExecutor::Options& options) {
  if (LLCAccessSpreader::get().numStripes() > 1) {
    return std::make_unique<
        StripedEDFPriorityBlockingQueue<CPUThreadPoolExecutor::CPUTask>>(
        options.tlsOptions);
  } else {
    return std::make_unique<
        EDFPriorityBlockingQueue<CPUThreadPoolExecutor::CPUTask>>(
        options.tlsOptions);
  }
}

} // namespace

StripedEDFThreadPoolExecutor::StripedEDFThreadPoolExecutor(
    std::pair<size_t, size_t> numThreads,
    std::shared_ptr<ThreadFactory> threadFactory,
    const Options& options)
    : CPUThreadPoolExecutor(
          numThreads, makeQueue(options), std::move(threadFactory)) {}

void StripedEDFThreadPoolExecutor::add(Func f, uint64_t deadline) {
  CPUTask task(std::move(f), {}, {}, 0);
  CPUThreadPoolExecutor::addImpl(
      [this, deadline](auto&& task) {
        using QueueType = BlockingQueueWithDeadline<CPUTask>;
        auto* taskQueue = static_cast<QueueType*>(getTaskQueue());
        DCHECK_EQ(taskQueue, dynamic_cast<QueueType*>(getTaskQueue()));
        return taskQueue->addWithDeadline(std::move(task), deadline);
      },
      std::move(task));
}

void StripedEDFThreadPoolExecutor::add(
    std::vector<Func> fs, uint64_t deadline) {
  // It might be possible to optimize this if we implement post(n) in
  // StripedThrottledLifoSem, but this variant is very rarely used.
  for (auto& f : fs) {
    add(std::move(f), deadline);
  }
}

} // namespace folly
