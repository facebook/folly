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

#pragma once

#include <memory>

#include <folly/Executor.h>
#include <folly/fibers/ExecutorBasedLoopController.h>
#include <folly/fibers/FiberManagerInternal.h>
#include <folly/futures/Future.h>

namespace folly {
namespace fibers {

class ExecutorTimeoutManager : public TimeoutManager {
 public:
  explicit ExecutorTimeoutManager(folly::Executor* executor)
      : executor_(executor) {}

  ExecutorTimeoutManager(ExecutorTimeoutManager&&) = default;
  ExecutorTimeoutManager& operator=(ExecutorTimeoutManager&&) = default;
  ExecutorTimeoutManager(const ExecutorTimeoutManager&) = delete;
  ExecutorTimeoutManager& operator=(const ExecutorTimeoutManager&) = delete;

  void attachTimeoutManager(
      AsyncTimeout* /* unused */,
      InternalEnum /* unused */) final {}

  void detachTimeoutManager(AsyncTimeout* /* unused */) final {
    throw std::logic_error(
        "detachTimeoutManager() call isn't supported by ExecutorTimeoutManager.");
  }

  bool scheduleTimeout(AsyncTimeout* obj, timeout_type timeout) final {
    folly::futures::sleep(timeout).via(executor_).thenValue(
        [obj](folly::Unit) { obj->timeoutExpired(); });
    return true;
  }

  void cancelTimeout(AsyncTimeout* /* unused */) final {
    throw std::logic_error(
        "cancelTimeout() call isn't supported by ExecutorTimeoutManager.");
  }

  void bumpHandlingTime() final {
    throw std::logic_error(
        "bumpHandlingTime() call isn't supported by ExecutorTimeoutManager.");
  }

  bool isInTimeoutManagerThread() final {
    throw std::logic_error(
        "isInTimeoutManagerThread() call isn't supported by ExecutorTimeoutManager.");
  }

 private:
  folly::Executor* executor_;
};

/**
 * A fiber loop controller that works for arbitrary folly::Executor
 */
class ExecutorLoopController : public fibers::ExecutorBasedLoopController {
 public:
  explicit ExecutorLoopController(folly::Executor* executor);
  ~ExecutorLoopController() override;

  folly::Executor* executor() const override {
    return executor_;
  }

 private:
  folly::Executor* executor_;
  Executor::KeepAlive<> executorKeepAlive_;
  fibers::FiberManager* fm_{nullptr};
  ExecutorTimeoutManager timeoutManager_;
  HHWheelTimer::UniquePtr timer_;

  void setFiberManager(fibers::FiberManager* fm) override;
  void schedule() override;
  void runLoop() override;
  void runEagerFiber(Fiber*) override;
  void scheduleThreadSafe() override;
  HHWheelTimer* timer() override;

  friend class fibers::FiberManager;
};

} // namespace fibers
} // namespace folly

#include <folly/fibers/ExecutorLoopController-inl.h>
