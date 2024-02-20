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

#include <folly/futures/HeapTimekeeper.h>

#include <optional>
#include <utility>
#include <vector>

#include <folly/container/IntrusiveHeap.h>
#include <folly/lang/SafeAssert.h>
#include <folly/synchronization/DistributedMutex.h>
#include <folly/synchronization/RelaxedAtomic.h>
#include <folly/synchronization/SaturatingSemaphore.h>
#include <folly/synchronization/WaitOptions.h>
#include <folly/system/ThreadName.h>

namespace folly {

class HeapTimekeeper::Timeout : public IntrusiveHeapNode<> {
 public:
  struct DecRef {
    void operator()(Timeout* timeout) const { timeout->decRef(); }
  };
  using Ref = std::unique_ptr<Timeout, DecRef>;

  static std::pair<Ref, SemiFuture<Unit>> create(
      HeapTimekeeper& parent, Clock::time_point expiration);

  void decRef();
  bool tryFulfill(Try<Unit> t);

  bool operator<(const Timeout& other) const {
    return expiration > other.expiration;
  }

  const Clock::time_point expiration;

 private:
  static void interruptHandler(
      Ref self, std::shared_ptr<State> state, exception_wrapper ew);

  Timeout(HeapTimekeeper& parent, Clock::time_point exp, Promise<Unit> promise);

  std::atomic<uint8_t> refCount_ = 2; // Heap and interrupt handler.
  relaxed_atomic<bool> fulfilled_ = false;
  Promise<Unit> promise_;
};

class HeapTimekeeper::State {
 public:
  struct Op {
    enum class Type { kSchedule, kCancel };

    Type type;
    Timeout::Ref timeout;
  };

  State() { clearAndAdjustCapacity(queue_); }
  ~State() {
    // State is shared with future, but it should only be destroyed once the
    // worker thread has drained it completely.
    CHECK_EQ(queue_.size(), 0);
    CHECK(heap_.empty());
  }

  static void clearAndAdjustCapacity(std::vector<Op>& queue);

  void enqueue(Op::Type type, Timeout::Ref&& timeout);
  void shutdown();

  void worker();

 private:
  using Semaphore = SaturatingSemaphore<>;

  static constexpr size_t kQueueBatchSize = 256;
  // Queue capacity is kept in this band to make sure that it is reallocated
  // under the lock as infrequently as possible.
  static constexpr size_t kDefaultQueueCapacity = 2 * kQueueBatchSize;
  static constexpr size_t kMaxQueueCapacity = 2 * kDefaultQueueCapacity;

  DistributedMutex mutex_;
  // These variables are synchronized using mutex_. nextWakeUp_ is only modified
  // by the worker thread, so it can be read back in that thread without a lock.
  bool stop_ = false;
  std::vector<Op> queue_;
  Clock::time_point nextWakeUp_ = Clock::time_point::max();
  Semaphore* wakeUp_ = nullptr;

  // Only accessed by the worker thread.
  IntrusiveHeap<Timeout> heap_;
};

/* static */ std::pair<HeapTimekeeper::Timeout::Ref, SemiFuture<Unit>>
HeapTimekeeper::Timeout::create(
    HeapTimekeeper& parent, Clock::time_point expiration) {
  auto [promise, sf] = makePromiseContract<Unit>();
  auto timeout =
      Timeout::Ref{new Timeout{parent, expiration, std::move(promise)}};
  return {std::move(timeout), std::move(sf)};
}

HeapTimekeeper::Timeout::Timeout(
    HeapTimekeeper& parent, Clock::time_point exp, Promise<Unit> promise)
    : expiration(exp), promise_(std::move(promise)) {
  promise_.setInterruptHandler(
      [self = Ref{this}, state = parent.state_](exception_wrapper ew) mutable {
        interruptHandler(std::move(self), std::move(state), std::move(ew));
      });
}

/* static */ void HeapTimekeeper::Timeout::interruptHandler(
    Ref self, std::shared_ptr<State> state, exception_wrapper ew) {
  if (!self->tryFulfill(Try<Unit>{std::move(ew)})) {
    return; // Timeout has already expired, nothing to do.
  }

  state->enqueue(State::Op::Type::kCancel, std::move(self));
}

bool HeapTimekeeper::Timeout::tryFulfill(Try<Unit> t) {
  if (fulfilled_.exchange(true)) {
    return false;
  }
  // Break the refcount cycle between promise and interrupt handler.
  auto promise = std::move(promise_);
  promise.setTry(std::move(t));
  return true;
}

void HeapTimekeeper::Timeout::decRef() {
  auto before = refCount_.fetch_sub(1, std::memory_order_acq_rel);
  FOLLY_SAFE_DCHECK(before > 0);
  if (before == 1) {
    delete this;
  }
}

/* static */ void HeapTimekeeper::State::clearAndAdjustCapacity(
    std::vector<Op>& queue) {
  queue.clear();
  if (queue.capacity() > kMaxQueueCapacity) {
    std::vector<Op>{}.swap(queue);
  }
  if (queue.capacity() < kDefaultQueueCapacity) {
    queue.reserve(kDefaultQueueCapacity);
  }
}

void HeapTimekeeper::State::enqueue(Op::Type type, Timeout::Ref&& timeout) {
  const auto* timeoutPtr = timeout.get();
  Op op;
  op.type = type;
  op.timeout = std::move(timeout);

  auto wakeUp = mutex_.lock_combine([&]() -> Semaphore* {
    if (stop_) {
      CHECK(type == Op::Type::kCancel)
          << "after() called on a destroying HeapTimekeeper";
      // If the timekeeper is shut down it won't process the queue anymore, so
      // just decRef the timeout inline, there is nothing to cancel.
      return nullptr;
    }

    queue_.push_back(std::move(op));
    if (wakeUp_ == nullptr) {
      // No semaphore set, so the worker thread won't go to sleep before
      // processing this op.
      return nullptr;
    }
    // Wake up the worker only if we have enough ops to process or we need to
    // update the wake-up time. We don't care about cancellations being
    // processed in a timely fashion, as the promise is already fulfilled, so we
    // can avoid an unnecessary wake-up.
    if (queue_.size() == kQueueBatchSize ||
        (type == Op::Type::kSchedule && nextWakeUp_ > timeoutPtr->expiration)) {
      // Signal that we are waking up the worker and others don't have to.
      return std::exchange(wakeUp_, nullptr);
    }

    return nullptr;
  });

  if (wakeUp) {
    wakeUp->post();
  }
}

void HeapTimekeeper::State::shutdown() {
  auto wakeUp = mutex_.lock_combine([&] {
    stop_ = true;
    return std::exchange(wakeUp_, nullptr);
  });
  if (wakeUp) {
    wakeUp->post();
  }
}

void HeapTimekeeper::State::worker() {
  setThreadName("FutureTimekeepr");
  std::vector<Op> queue;
  while (true) {
    clearAndAdjustCapacity(queue);
    std::optional<Semaphore> wakeUp;
    bool stop = false;
    mutex_.lock_combine([&] {
      FOLLY_SAFE_DCHECK(wakeUp_ == nullptr);

      if (!queue_.empty()) {
        queue_.swap(queue);
        return;
      }
      if (stop_) {
        // Only stop if the queue is empty, as we need to manage the lifetime of
        // the timeouts in it.
        stop = true;
        return;
      }

      // No queue to process, wait for the next timeout, but allow callers to
      // wake us up if we need to update the next wakeup time.
      wakeUp.emplace();
      wakeUp_ = &*wakeUp;
      nextWakeUp_ =
          heap_.empty() ? Clock::time_point::max() : heap_.top()->expiration;
    });

    if (stop) {
      break;
    }

    if (wakeUp) {
      WaitOptions wo;
      // It's very likely that the wait will timeout, so there is no point in
      // spinning unless the wait time is shorter than the default spin time.
      wo.spin_max(
          nextWakeUp_ - Clock::now() > wo.spin_max()
              ? std::chrono::nanoseconds{0}
              : wo.spin_max());
      if (!wakeUp->try_wait_until(nextWakeUp_)) {
        if (mutex_.lock_combine(
                [&] { return std::exchange(wakeUp_, nullptr) == nullptr; })) {
          // Someone stole the reference to the semaphore, we must wait for them
          // to post it so we can destroy it.
          wakeUp->wait();
        }
      }
    }

    for (auto& op : queue) {
      switch (op.type) {
        case Op::Type::kSchedule:
          heap_.push(op.timeout.release()); // Heap takes ownership.
          break;
        case Op::Type::kCancel:
          if (op.timeout->isLinked()) {
            heap_.erase(op.timeout.get());
            op.timeout->decRef();
          }
          break;
      }
    }

    while (!heap_.empty() && heap_.top()->expiration <= Clock::now()) {
      auto* timeout = heap_.pop();
      timeout->tryFulfill(Try<Unit>{unit});
      timeout->decRef();
    }
  }

  // Cancel all the leftover timeouts.
  while (!heap_.empty()) {
    auto* timeout = heap_.pop();
    timeout->tryFulfill(Try<Unit>{exception_wrapper{FutureNoTimekeeper{}}});
    timeout->decRef();
  }
}

HeapTimekeeper::HeapTimekeeper() : state_(std::make_shared<State>()) {
  thread_ = std::thread{[this] { state_->worker(); }};
}

HeapTimekeeper::~HeapTimekeeper() {
  state_->shutdown();
  thread_.join();
}

SemiFuture<Unit> HeapTimekeeper::after(HighResDuration dur) {
  auto [timeout, sf] = Timeout::create(*this, Clock::now() + dur);
  state_->enqueue(State::Op::Type::kSchedule, std::move(timeout));
  return std::move(sf);
}

} // namespace folly
