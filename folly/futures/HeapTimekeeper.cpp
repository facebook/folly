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

#include <folly/lang/SafeAssert.h>
#include <folly/synchronization/WaitOptions.h>
#include <folly/system/ThreadName.h>

namespace folly {

HeapTimekeeper::HeapTimekeeper() {
  clearAndAdjustCapacity(queue_);
  thread_ = std::thread{[this] { worker(); }};
}

HeapTimekeeper::~HeapTimekeeper() {
  auto wakeUp = mutex_.lock_combine([&] {
    stop_ = true;
    return std::exchange(wakeUp_, nullptr);
  });
  if (wakeUp) {
    wakeUp->post();
  }
  thread_.join();
}

SemiFuture<Unit> HeapTimekeeper::after(HighResDuration dur) {
  // TODO(ott): Add keepalive relationship on the timekeeper.
  auto [timeout, sf] = Timeout::create(*this, Clock::now() + dur);
  Op op;
  op.type = Op::Type::kSchedule;
  op.timeout = timeout;
  enqueue(op);
  return std::move(sf);
}

void HeapTimekeeper::enqueue(Op op) {
  auto wakeUp = mutex_.lock_combine([&]() -> Semaphore* {
    queue_.push_back(op);
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
        (op.type == Op::Type::kSchedule &&
         nextWakeUp_ > op.timeout->expiration)) {
      // Signal that we are waking up the worker and others don't have to.
      return std::exchange(wakeUp_, nullptr);
    }

    return nullptr;
  });
  if (wakeUp) {
    wakeUp->post();
  }
}

void HeapTimekeeper::worker() {
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

    for (const auto& op : queue) {
      switch (op.type) {
        case Op::Type::kSchedule:
          heap_.push(op.timeout);
          break;
        case Op::Type::kCancel:
          if (op.timeout->isLinked()) {
            heap_.erase(op.timeout);
            op.timeout->release();
          }
          op.timeout->release();
          break;
      }
    }

    while (!heap_.empty() && heap_.top()->expiration <= Clock::now()) {
      auto* timeout = heap_.pop();
      timeout->tryFulfill(Try<Unit>{unit});
      timeout->release();
    }
  }

  // Cancel all the leftover timeouts.
  while (!heap_.empty()) {
    auto* timeout = heap_.pop();
    timeout->tryFulfill(Try<Unit>{exception_wrapper{FutureNoTimekeeper{}}});
    timeout->release();
  }
}

/* static */ void HeapTimekeeper::clearAndAdjustCapacity(
    std::vector<Op>& queue) {
  queue.clear();
  if (queue.capacity() > kMaxQueueCapacity) {
    std::vector<Op>{}.swap(queue);
  }
  if (queue.capacity() < kDefaultQueueCapacity) {
    queue.reserve(kDefaultQueueCapacity);
  }
}

/* static */ std::pair<HeapTimekeeper::Timeout*, SemiFuture<Unit>>
HeapTimekeeper::Timeout::create(
    HeapTimekeeper& timekeeper, Clock::time_point expiration) {
  auto [promise, sf] = makePromiseContract<Unit>();
  auto* timeout = new Timeout{timekeeper, expiration, std::move(promise)};
  return {timeout, std::move(sf)};
}

HeapTimekeeper::Timeout::Timeout(
    HeapTimekeeper& timekeeper, Clock::time_point exp, Promise<Unit> promise)
    : expiration(exp), promise_(std::move(promise)) {
  promise_.setInterruptHandler(
      [self = Ref{this}, &timekeeper](exception_wrapper ew) mutable {
        interruptHandler(std::move(self), timekeeper, std::move(ew));
      });
}

/* static */ void HeapTimekeeper::Timeout::interruptHandler(
    Ref self, HeapTimekeeper& timekeeper, exception_wrapper ew) {
  if (!self->tryFulfill(Try<Unit>{std::move(ew)})) {
    return; // Timeout has already expired, nothing to do.
  }

  Op op;
  op.type = Op::Type::kCancel;
  op.timeout = self.release(); // Pass ownership to the worker thread.
  timekeeper.enqueue(op);
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

void HeapTimekeeper::Timeout::release() {
  auto before = refCount_.fetch_sub(1, std::memory_order_acq_rel);
  FOLLY_SAFE_DCHECK(before > 0);
  if (before == 1) {
    delete this;
  }
}

} // namespace folly
