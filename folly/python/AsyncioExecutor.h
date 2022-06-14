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

#pragma once

#include <Python.h>
#include <folly/ExceptionString.h>
#include <folly/Function.h>
#include <folly/executors/DrivableExecutor.h>
#include <folly/executors/SequencedExecutor.h>
#include <folly/io/async/NotificationQueue.h>

#if PY_VERSION_HEX <= 0x03070000
#define FOLLY_DETAIL_PY_ISFINALIZING() false
#else
#define FOLLY_DETAIL_PY_ISFINALIZING() _Py_IsFinalizing()
#endif

namespace folly {
namespace python {

class AsyncioExecutor : public DrivableExecutor, public SequencedExecutor {
 public:
  virtual ~AsyncioExecutor() override {
    keepAliveRelease();
    while (keepAliveCounter_ > 0) {
      drive();
    }
  }

  void drive() noexcept final {
    if (FOLLY_DETAIL_PY_ISFINALIZING()) {
      // if Python is finalizing calling scheduled functions MAY segfault.
      // any code that could have been called is now inconsequential.
      return;
    }
    driveNoDiscard();
  }

  virtual void driveNoDiscard() noexcept = 0;

 protected:
  bool keepAliveAcquire() noexcept override {
    auto keepAliveCounter =
        keepAliveCounter_.fetch_add(1, std::memory_order_relaxed);
    // We should never increment from 0
    DCHECK(keepAliveCounter > 0);
    return true;
  }

  void keepAliveRelease() noexcept override {
    auto keepAliveCounter = --keepAliveCounter_;
    DCHECK(keepAliveCounter >= 0);
  }

 private:
  std::atomic<size_t> keepAliveCounter_{1};
};

class NotificationQueueAsyncioExecutor : public AsyncioExecutor {
 public:
  using Func = folly::Func;

  void add(Func func) override { queue_.putMessage(std::move(func)); }

  int fileno() const { return consumer_.getFd(); }

  void driveNoDiscard() noexcept override {
    consumer_.consume([&](Func&& func) {
      invokeCatchingExns(
          "NotificationQueueExecutor: task", std::exchange(func, {}));
    });
  }

  folly::NotificationQueue<Func> queue_;
  folly::NotificationQueue<Func>::SimpleConsumer consumer_{queue_};
}; // NotificationQueueAsyncioExecutor

} // namespace python
} // namespace folly
