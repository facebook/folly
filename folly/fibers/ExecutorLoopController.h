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
      AsyncTimeout* /* unused */, InternalEnum /* unused */) final {}

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

  folly::Executor* executor() const override { return executor_; }

 private:
  folly::Executor* executor_;
  Executor::KeepAlive<> executorKeepAlive_;
  fibers::FiberManager* fm_{nullptr};
  ExecutorTimeoutManager timeoutManager_;
  HHWheelTimer::UniquePtr timer_;
  std::atomic<std::thread::id> loopThread_;

  class LocalCallbackControlBlock {
   public:
    struct DeleteOrCancel {
      void operator()(LocalCallbackControlBlock* controlBlock) {
        if (controlBlock->scheduled_) {
          controlBlock->cancelled_ = true;
        } else {
          delete controlBlock;
        }
      }
    };
    using Ptr = std::unique_ptr<LocalCallbackControlBlock, DeleteOrCancel>;

    struct GuardDeleter {
      void operator()(LocalCallbackControlBlock* controlBlock) {
        DCHECK(controlBlock->scheduled_);
        controlBlock->scheduled_ = false;
        if (controlBlock->cancelled_) {
          delete controlBlock;
        }
      }
    };
    using Guard = std::unique_ptr<LocalCallbackControlBlock, GuardDeleter>;

    static Ptr create() { return Ptr(new LocalCallbackControlBlock()); }

    Guard trySchedule() {
      if (scheduled_) {
        return {};
      }
      scheduled_ = true;
      return Guard(this);
    }

    bool isCancelled() const { return cancelled_; }

   private:
    LocalCallbackControlBlock() {}

    bool cancelled_{false};
    bool scheduled_{false};
  };
  LocalCallbackControlBlock::Ptr localCallbackControlBlock_{
      LocalCallbackControlBlock::create()};

  void setFiberManager(fibers::FiberManager* fm) override;
  void schedule() override;
  void runLoop() override;
  void runEagerFiber(Fiber*) override;
  void scheduleThreadSafe() override;
  HHWheelTimer* timer() override;
  bool isInLoopThread() override {
    return loopThread_ == std::this_thread::get_id();
  }

  friend class fibers::FiberManager;
};

} // namespace fibers
} // namespace folly

#include <folly/fibers/ExecutorLoopController-inl.h>
