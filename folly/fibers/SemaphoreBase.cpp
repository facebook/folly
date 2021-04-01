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

#include <folly/fibers/SemaphoreBase.h>

namespace folly {
namespace fibers {

bool SemaphoreBase::signalSlow(int64_t tokens, int64_t oldVal) {
  Waiter* waiter = nullptr;
  {
    // If we signalled a release, notify the waitlist
    auto waitListLock = waitList_.wlock();
    auto& waitList = *waitListLock;

    auto testVal = tokens_.load(std::memory_order_acquire);

    if (oldVal != testVal) {
      return false;
    }

    if (waitList.empty()) {
      // If the waitlist is now empty and tokens is 0, ensure the token count
      // increments No need for CAS here as we will always be under the mutex
      return tokens_.compare_exchange_strong(
          testVal, testVal + tokens, std::memory_order_relaxed);
    }

    waiter = &waitList.front();
    if (waiter->tokens_ > testVal + tokens) {
      // Not enough tokens to resume next in waitlist
      return tokens_.compare_exchange_strong(
          testVal, testVal + tokens, std::memory_order_relaxed);
    }
    waitList.pop_front();
  }
  // Trigger waiter if there is one
  // Do it after releasing the waitList mutex, in case the waiter
  // eagerly calls signal
  waiter->baton.post();
  return true;
}

bool SemaphoreBase::waitSlow(Waiter& waiter, int64_t tokens) {
  // Slow path, create a baton and acquire a mutex to update the wait list
  {
    auto waitListLock = waitList_.wlock();
    auto& waitList = *waitListLock;

    auto testVal = tokens_.load(std::memory_order_acquire);
    if (testVal >= tokens) {
      return false;
    }
    // prepare baton and add to queue
    waitList.push_back(waiter);
    assert(!waitList.empty());
  }
  // Signal to caller that we managed to push a waiter
  return true;
}

void SemaphoreBase::wait_common(int64_t tokens) {
  auto oldVal = tokens_.load(std::memory_order_acquire);
  do {
    while (oldVal < tokens) {
      Waiter waiter{tokens};
      // If waitSlow fails it is because the capacity is greater than requested
      // by the time the lock is taken, so we can just continue round the loop
      if (waitSlow(waiter, tokens)) {
        waiter.baton.wait();
        return;
      }
      oldVal = tokens_.load(std::memory_order_acquire);
    }
  } while (!tokens_.compare_exchange_weak(
      oldVal,
      oldVal - tokens,
      std::memory_order_release,
      std::memory_order_acquire));
}

bool SemaphoreBase::try_wait_common(Waiter& waiter, int64_t tokens) {
  auto oldVal = tokens_.load(std::memory_order_acquire);
  do {
    while (oldVal < tokens) {
      if (waitSlow(waiter, tokens)) {
        return false;
      }
      oldVal = tokens_.load(std::memory_order_acquire);
    }
  } while (!tokens_.compare_exchange_weak(
      oldVal,
      oldVal - tokens,
      std::memory_order_release,
      std::memory_order_acquire));
  return true;
}

#if FOLLY_HAS_COROUTINES

coro::Task<void> SemaphoreBase::co_wait_common(int64_t tokens) {
  auto oldVal = tokens_.load(std::memory_order_acquire);
  do {
    while (oldVal < tokens) {
      Waiter waiter{tokens};
      // If waitSlow fails it is because the capacity is greater than requested
      // by the time the lock is taken, so we can just continue round the loop
      if (waitSlow(waiter, tokens)) {
        bool cancelled = false;
        {
          const auto& ct = co_await folly::coro::co_current_cancellation_token;
          folly::CancellationCallback cb{
              ct, [&] {
                {
                  auto waitListLock = waitList_.wlock();
                  auto& waitList = *waitListLock;

                  if (!waiter.hook_.is_linked()) {
                    // Already dequeued by signalSlow()
                    return;
                  }

                  cancelled = true;
                  waitList.erase(waitList.iterator_to(waiter));
                }

                waiter.baton.post();
              }};

          co_await waiter.baton;
        }

        // Check 'cancelled' flag only after deregistering the callback so we're
        // sure that we aren't reading it concurrently with a potential write
        // from a thread requesting cancellation.
        if (cancelled) {
          co_yield folly::coro::co_cancelled;
        }

        co_return;
      }
      oldVal = tokens_.load(std::memory_order_acquire);
    }
  } while (!tokens_.compare_exchange_weak(
      oldVal,
      oldVal - tokens,
      std::memory_order_release,
      std::memory_order_acquire));
}

#endif

namespace {

class FutureWaiter final : public fibers::Baton::Waiter {
 public:
  explicit FutureWaiter(int64_t tokens) : semaphoreWaiter(tokens) {
    semaphoreWaiter.baton.setWaiter(*this);
  }

  void post() override {
    std::unique_ptr<FutureWaiter> destroyOnReturn{this};
    promise.setValue();
  }

  SemaphoreBase::Waiter semaphoreWaiter;
  folly::Promise<Unit> promise;
};

} // namespace

SemiFuture<Unit> SemaphoreBase::future_wait_common(int64_t tokens) {
  auto oldVal = tokens_.load(std::memory_order_acquire);
  do {
    while (oldVal < tokens) {
      auto batonWaiterPtr = std::make_unique<FutureWaiter>(tokens);
      // If waitSlow fails it is because the capacity is greater than requested
      // by the time the lock is taken, so we can just continue round the loop
      auto future = batonWaiterPtr->promise.getSemiFuture();
      if (waitSlow(batonWaiterPtr->semaphoreWaiter, tokens)) {
        (void)batonWaiterPtr.release();
        return future;
      }
      oldVal = tokens_.load(std::memory_order_acquire);
    }
  } while (!tokens_.compare_exchange_weak(
      oldVal,
      oldVal - tokens,
      std::memory_order_release,
      std::memory_order_acquire));
  return makeSemiFuture();
}

size_t SemaphoreBase::getCapacity() const {
  return capacity_;
}

} // namespace fibers
} // namespace folly
