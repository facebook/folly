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

#include <glog/logging.h>

#include <folly/ExceptionString.h>

namespace folly::detail {

template <template <typename> typename Queue>
class SerialExecutorImpl<Queue>::Worker {
 public:
  explicit Worker(KeepAlive<SerialExecutorImpl> ka) : ka_(std::move(ka)) {}

  ~Worker() {
    if (ka_) {
      ka_->drain(); // We own the queue but we did not run.
    }
  }

  Worker(Worker&& other) : ka_(std::exchange(other.ka_, {})) {}

  Worker(const Worker&) = delete;
  Worker& operator=(const Worker&) = delete;
  Worker& operator=(Worker&&) = delete;

  void operator()() { std::exchange(ka_, {})->worker(); }

 private:
  KeepAlive<SerialExecutorImpl> ka_;
};

template <template <typename> typename Queue>
SerialExecutorImpl<Queue>::SerialExecutorImpl(KeepAlive<Executor> parent)
    : parent_(std::move(parent)) {}

template <template <typename> typename Queue>
SerialExecutorImpl<Queue>::~SerialExecutorImpl() {
  DCHECK(!keepAliveCounter_);
}

template <template <typename> typename Queue>
Executor::KeepAlive<SerialExecutorImpl<Queue>>
SerialExecutorImpl<Queue>::create(KeepAlive<Executor> parent) {
  return makeKeepAlive<SerialExecutorImpl<Queue>>(
      new SerialExecutorImpl<Queue>(std::move(parent)));
}

template <template <typename> typename Queue>
typename SerialExecutorImpl<Queue>::UniquePtr
SerialExecutorImpl<Queue>::createUnique(std::shared_ptr<Executor> parent) {
  auto executor =
      new SerialExecutorImpl<Queue>(getKeepAliveToken(parent.get()));
  return {executor, Deleter{std::move(parent)}};
}

template <template <typename> typename Queue>
bool SerialExecutorImpl<Queue>::keepAliveAcquire() noexcept {
  auto keepAliveCounter =
      keepAliveCounter_.fetch_add(1, std::memory_order_relaxed);
  DCHECK(keepAliveCounter > 0);
  return true;
}

template <template <typename> typename Queue>
void SerialExecutorImpl<Queue>::keepAliveRelease() noexcept {
  auto keepAliveCounter =
      keepAliveCounter_.fetch_sub(1, std::memory_order_acq_rel);
  DCHECK(keepAliveCounter > 0);
  if (keepAliveCounter == 1) {
    delete this;
  }
}

template <template <typename> typename Queue>
void SerialExecutorImpl<Queue>::add(Func func) {
  if (scheduleTask(std::move(func))) {
    parent_->add(Worker{getKeepAliveToken(this)});
  }
}

template <template <typename> typename Queue>
void SerialExecutorImpl<Queue>::addWithPriority(Func func, int8_t priority) {
  if (scheduleTask(std::move(func))) {
    parent_->addWithPriority(Worker{getKeepAliveToken(this)}, priority);
  }
}

template <template <typename> typename Queue>
bool SerialExecutorImpl<Queue>::scheduleTask(Func&& func) {
  queue_.enqueue(Task{std::move(func), RequestContext::saveContext()});
  // If this thread is the first to mark the queue as non-empty, schedule the
  // worker.
  return scheduled_.fetch_add(1, std::memory_order_acq_rel) == 0;
}

template <template <typename> typename Queue>
void SerialExecutorImpl<Queue>::worker() {
  std::size_t queueSize = scheduled_.load(std::memory_order_acquire);
  DCHECK_NE(queueSize, 0);

  std::size_t processed = 0;
  while (true) {
    Task task;
    queue_.dequeue(task);
    folly::RequestContextScopeGuard ctxGuard(std::move(task.ctx));
    invokeCatchingExns("SerialExecutor: func", std::exchange(task.func, {}));

    if (++processed == queueSize) {
      // NOTE: scheduled_ must be decremented after the task has been processed,
      // or add() may concurrently start another worker.
      queueSize = scheduled_.fetch_sub(queueSize, std::memory_order_acq_rel) -
          queueSize;
      if (queueSize == 0) {
        // Queue is now empty
        return;
      }
      processed = 0;
    }
  }
}

template <template <typename> typename Queue>
void SerialExecutorImpl<Queue>::drain() {
  auto queueSize = scheduled_.load(std::memory_order_acquire);
  while (queueSize != 0) {
    Task task;
    queue_.dequeue(task);
    queueSize = scheduled_.fetch_sub(1, std::memory_order_acq_rel) - 1;
  }
}

/**
 * MPSC queue with the additional requirement that the queue must be non-empty
 * on dequeue(), and the enqueue() that makes the queue non-empty must complete
 * before the corresponding dequeue(). This is guaranteed in SerialExecutor by
 * release/acquire synchronization on scheduled_.
 *
 * Producers are internally synchronized using a mutex, while the consumer
 * relies entirely on external synchronization.
 */
template <class Task>
class SerialExecutorMPSCQueue {
  static_assert(std::is_nothrow_move_constructible_v<Task>);

 public:
  ~SerialExecutorMPSCQueue() {
    // Queue must be consumed completely at destruction.
    CHECK_EQ(head_, tail_);
    CHECK_EQ(head_->readIdx.load(), head_->writeIdx.load());
    CHECK(head_->next == nullptr);
    delete head_;
    delete segmentCache_.load(std::memory_order_acquire);
  }

  void enqueue(Task&& task) {
    mutex_.lock_combine([&] {
      // dequeue() will not delete a segment or try to read head_->next until
      // the next write has completed, so this is safe.
      if (tail_->writeIdx.load() == kSegmentSize) {
        auto* segment =
            segmentCache_.exchange(nullptr, std::memory_order_acquire);
        if (segment == nullptr) {
          segment = new Segment;
        } else {
          std::destroy_at(segment);
          new (segment) Segment;
        }
        tail_->next = segment;
        tail_ = segment;
      }
      auto idx = tail_->writeIdx.load();
      new (&tail_->tasks[idx]) Task(std::move(task));
      tail_->writeIdx.store(idx + 1);
    });
  }

  void dequeue(Task& task) {
    auto idx = head_->readIdx.load();
    DCHECK_LE(idx, kSegmentSize);
    if (idx == kSegmentSize) {
      DCHECK(head_->next != nullptr);
      auto* oldSegment = std::exchange(head_, head_->next);
      // If there is already a segment in cache, replace it with the latest one,
      // as it is more likely to still be warm in cache for the producer.
      delete segmentCache_.exchange(oldSegment, std::memory_order_release);
      DCHECK_EQ(head_->readIdx.load(), 0);
      idx = 0;
    }
    DCHECK_LT(idx, head_->writeIdx.load());
    task = std::move(*reinterpret_cast<Task*>(&head_->tasks[idx]));
    std::destroy_at(&head_->tasks[idx]);
    head_->readIdx.store(idx + 1);
  }

 private:
  static constexpr size_t kSegmentSize = 16;

  struct Segment {
    // Neither writeIdx or readIdx need to be atomic since each is exclusively
    // owned by respectively producer and consumer, but we need atomicity for
    // assertions.
    // We avoid any padding to minimize memory usage, but at least we can
    // separate write and read index by interposing the payloads.
    relaxed_atomic<size_t> writeIdx = 0;
    std::aligned_storage_t<sizeof(Task), alignof(Task)> tasks[kSegmentSize];
    relaxed_atomic<size_t> readIdx = 0;
    Segment* next = nullptr;
  };
  static_assert(std::is_trivially_destructible_v<Segment>);

  folly::DistributedMutex mutex_;
  Segment* tail_ = new Segment;
  Segment* head_ = tail_;
  // Cache the allocation for exactly one segment, so that in the common case
  // where the consumer keeps up with the producer no allocations are needed.
  std::atomic<Segment*> segmentCache_{nullptr};
};

} // namespace folly::detail
