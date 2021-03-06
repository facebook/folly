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

#include <atomic>
#include <memory>

#include <folly/CancellationToken.h>
#include <folly/fibers/ExecutorBasedLoopController.h>
#include <folly/fibers/FiberManagerInternal.h>
#include <folly/io/async/VirtualEventBase.h>

namespace folly {
namespace fibers {

class EventBaseLoopController : public ExecutorBasedLoopController {
 public:
  explicit EventBaseLoopController();
  ~EventBaseLoopController() override;

  /**
   * Attach EventBase after LoopController was created.
   */
  void attachEventBase(EventBase& eventBase);
  void attachEventBase(VirtualEventBase& eventBase);

  VirtualEventBase* getEventBase() { return eventBase_; }

  void setLoopRunner(InlineFunctionRunner* loopRunner) {
    loopRunner_ = loopRunner;
  }

  folly::Executor* executor() const override { return eventBase_; }

 private:
  class ControllerCallback : public folly::EventBase::LoopCallback {
   public:
    explicit ControllerCallback(EventBaseLoopController& controller)
        : controller_(controller) {}

    void runLoopCallback() noexcept override { controller_.runLoop(); }

   private:
    EventBaseLoopController& controller_;
  };

  folly::CancellationToken eventBaseShutdownToken_;

  bool awaitingScheduling_{false};
  VirtualEventBase* eventBase_{nullptr};
  Executor::KeepAlive<VirtualEventBase> eventBaseKeepAlive_;
  ControllerCallback callback_;
  FiberManager* fm_{nullptr};
  std::atomic<bool> eventBaseAttached_{false};
  InlineFunctionRunner* loopRunner_{nullptr};

  /* LoopController interface */

  void setFiberManager(FiberManager* fm) override;
  void schedule() override;
  void runLoop() override;
  void runEagerFiber(Fiber*) override;
  void scheduleThreadSafe() override;
  HHWheelTimer* timer() override;

  friend class FiberManager;
};
} // namespace fibers
} // namespace folly

#include <folly/fibers/EventBaseLoopController-inl.h>
