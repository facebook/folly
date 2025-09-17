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

#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>

#include <folly/executors/DrivableExecutor.h>
#include <folly/executors/SequencedExecutor.h>

namespace folly {
namespace python {

/**
 * A simple ManualExecutor intended to be run directly on a Python thread.
 * It releases Python GIL while waiting for tasks to execute.
 */
class GILAwareManualExecutor
    : public DrivableExecutor,
      public SequencedExecutor {
 public:
  ~GILAwareManualExecutor() override;

  void add(Func) override;

  void drive() override {
    waitBeforeDrive();
    driveImpl();
  }

  bool keepAliveAcquire() noexcept override {
    keepAliveCount_.fetch_add(1, std::memory_order_relaxed);
    return true;
  }

  void keepAliveRelease() noexcept override {
    auto keepAliveCount = keepAliveCount_.load(std::memory_order_relaxed);
    do {
      if (keepAliveCount == 1) {
        add([this] {
          // the final count *must* be released from this executor or else if we
          // are mid-destructor we have a data race
          keepAliveCount_.fetch_sub(1, std::memory_order_relaxed);
        });
        return;
      }
    } while (!keepAliveCount_.compare_exchange_weak(
        keepAliveCount,
        keepAliveCount - 1,
        std::memory_order_release,
        std::memory_order_relaxed));
  }

 private:
  void waitBeforeDrive();
  void driveImpl();

  std::mutex lock_;
  std::queue<Func> funcs_;
  std::condition_variable cv_;

  std::atomic<size_t> keepAliveCount_{0};
};

} // namespace python
} // namespace folly
