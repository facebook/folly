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

#include <folly/fibers/Semaphore.h>

namespace folly {
namespace fibers {

bool Semaphore::signalSlow() {
  Baton* waiter = nullptr;
  {
    // If we signalled a release, notify the waitlist
    auto waitListLock = waitList_.wlock();
    auto& waitList = *waitListLock;

    auto testVal = tokens_.load(std::memory_order_acquire);
    if (testVal != 0) {
      return false;
    }

    if (waitList.empty()) {
      // If the waitlist is now empty, ensure the token count increments
      // No need for CAS here as we will always be under the mutex
      CHECK(tokens_.compare_exchange_strong(
          testVal, testVal + 1, std::memory_order_relaxed));
      return true;
    }
    waiter = waitList.front();
    waitList.pop();
  }
  // Trigger waiter if there is one
  // Do it after releasing the waitList mutex, in case the waiter
  // eagerly calls signal
  waiter->post();
  return true;
}

void Semaphore::signal() {
  auto oldVal = tokens_.load(std::memory_order_acquire);
  do {
    while (oldVal == 0) {
      if (signalSlow()) {
        return;
      }
      oldVal = tokens_.load(std::memory_order_acquire);
    }
  } while (!tokens_.compare_exchange_weak(
      oldVal,
      oldVal + 1,
      std::memory_order_release,
      std::memory_order_acquire));
}

bool Semaphore::waitSlow(folly::fibers::Baton& waitBaton) {
  // Slow path, create a baton and acquire a mutex to update the wait list
  {
    auto waitListLock = waitList_.wlock();
    auto& waitList = *waitListLock;

    auto testVal = tokens_.load(std::memory_order_acquire);
    if (testVal != 0) {
      return false;
    }
    // prepare baton and add to queue
    waitList.push(&waitBaton);
  }
  // Signal to caller that we managed to push a baton
  return true;
}

void Semaphore::wait() {
  auto oldVal = tokens_.load(std::memory_order_acquire);
  do {
    while (oldVal == 0) {
      folly::fibers::Baton waitBaton;
      // If waitSlow fails it is because the token is non-zero by the time
      // the lock is taken, so we can just continue round the loop
      if (waitSlow(waitBaton)) {
        waitBaton.wait();
        return;
      }
      oldVal = tokens_.load(std::memory_order_acquire);
    }
  } while (!tokens_.compare_exchange_weak(
      oldVal,
      oldVal - 1,
      std::memory_order_release,
      std::memory_order_acquire));
}

bool Semaphore::try_wait(Baton& waitBaton) {
  auto oldVal = tokens_.load(std::memory_order_acquire);
  do {
    while (oldVal == 0) {
      if (waitSlow(waitBaton)) {
        return false;
      }
      oldVal = tokens_.load(std::memory_order_acquire);
    }
  } while (!tokens_.compare_exchange_weak(
      oldVal,
      oldVal - 1,
      std::memory_order_release,
      std::memory_order_acquire));
  return true;
}

#if FOLLY_HAS_COROUTINES

coro::Task<void> Semaphore::co_wait() {
  auto oldVal = tokens_.load(std::memory_order_acquire);
  do {
    while (oldVal == 0) {
      folly::fibers::Baton waitBaton;
      // If waitSlow fails it is because the token is non-zero by the time
      // the lock is taken, so we can just continue round the loop
      if (waitSlow(waitBaton)) {
        co_await waitBaton;
        co_return;
      }
      oldVal = tokens_.load(std::memory_order_acquire);
    }
  } while (!tokens_.compare_exchange_weak(
      oldVal,
      oldVal - 1,
      std::memory_order_release,
      std::memory_order_acquire));
}

#endif

#if FOLLY_FUTURE_USING_FIBER

SemiFuture<Unit> Semaphore::future_wait() {
  auto oldVal = tokens_.load(std::memory_order_acquire);
  do {
    while (oldVal == 0) {
      auto waitBaton = std::make_unique<fibers::Baton>();
      // If waitSlow fails it is because the token is non-zero by the time
      // the lock is taken, so we can just continue round the loop
      if (waitSlow(*waitBaton)) {
        return futures::wait(std::move(waitBaton));
      }
      oldVal = tokens_.load(std::memory_order_acquire);
    }
  } while (!tokens_.compare_exchange_weak(
      oldVal,
      oldVal - 1,
      std::memory_order_release,
      std::memory_order_acquire));
  return makeSemiFuture();
}

#endif

size_t Semaphore::getCapacity() const {
  return capacity_;
}

} // namespace fibers
} // namespace folly
