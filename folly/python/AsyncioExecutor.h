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

namespace folly {
namespace python {

class AsyncioExecutor : public DrivableExecutor {
 public:
  ~AsyncioExecutor() override { DCHECK_EQ(keepAliveCounter_, 0); }

 protected:
  /**
   * This function must be called before the parent python loop is closed. It
   * takes care of draining any pending callbacks and destroying the executor
   * instance.
   */
  void drop() noexcept {
    keepAliveRelease();

    /**
     * If python is finalizing, calling scheduled functions MAY segfault.
     * Any code that could have been called is now inconsequential.
     */
    if (!isPyFinalizing()) {
      while (keepAliveCounter_ > 0) {
        drive();
        // Note: We're busy waiting for new callbacks (not ideal at all)
      }
    }

    /**
     * If there are pending keep-alives, it is not safe to delete *this*.
     * Leak it instead: This is done to ensure that the pending keep-alive
     * pointers are still valid i.e. we cannot destroy this object if the
     * ref-count > 0
     */
    if (keepAliveCounter_ == 0) {
      delete this;
    }
  }

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
  static bool isPyFinalizing() noexcept {
#if PY_VERSION_HEX <= 0x03070000
    return false;
#else
    return _Py_IsFinalizing();
#endif
  }

  std::atomic<size_t> keepAliveCounter_{1};
};

// Helper to ensure that `drop` is called reliably
template <typename Derived>
class DroppableAsyncioExecutor : public AsyncioExecutor {
 public:
  struct Deleter {
    void operator()(Derived* ptr) { ptr->drop(); }
  };

  using PtrType = std::unique_ptr<Derived, Deleter>;

  template <typename... Args>
  static PtrType create(Args&&... args) noexcept {
    return PtrType(new Derived(std::forward<Args>(args)...), Deleter());
  }
};

class NotificationQueueAsyncioExecutor
    : public DroppableAsyncioExecutor<NotificationQueueAsyncioExecutor>,
      public SequencedExecutor {
 public:
  using Func = folly::Func;

  void add(Func func) override { queue_.putMessage(std::move(func)); }

  int fileno() const { return consumer_.getFd(); }

  void drive() noexcept override {
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
