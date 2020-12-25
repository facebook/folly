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

#include <folly/Function.h>
#include <folly/Likely.h>
#include <folly/fibers/FiberManager.h>
#include <folly/fibers/LoopController.h>

namespace folly {
namespace fibers {

class FiberManager;

class SimpleLoopController : public LoopController {
 public:
  SimpleLoopController();
  ~SimpleLoopController();

  /**
   * Run FiberManager loop; if no ready task are present,
   * run provided function. Stops after both stop() has been called
   * and no waiting tasks remain.
   */
  template <typename F>
  void loop(F&& func) {
    loopThread_.store(std::this_thread::get_id(), std::memory_order_release);

    bool waiting = false;
    stopRequested_ = false;

    while (LIKELY(waiting || !stopRequested_)) {
      func();
      runTimeouts();
      if (scheduled_) {
        scheduled_ = false;
        runLoop();
        waiting = fm_->hasTasks();
      }
    }

    loopThread_.store({}, std::memory_order_release);
  }

  /**
   * Requests exit from loop() as soon as all waiting tasks complete.
   */
  void stop() { stopRequested_ = true; }

  int remoteScheduleCalled() const { return remoteScheduleCalled_; }

  void runLoop() override {
    do {
      if (remoteLoopRun_ < remoteScheduleCalled_) {
        for (; remoteLoopRun_ < remoteScheduleCalled_; ++remoteLoopRun_) {
          if (fm_->shouldRunLoopRemote()) {
            fm_->loopUntilNoReadyImpl();
          }
        }
      } else {
        fm_->loopUntilNoReadyImpl();
      }
    } while (remoteLoopRun_ < remoteScheduleCalled_);
  }

  void runEagerFiber(Fiber* fiber) override { fm_->runEagerFiberImpl(fiber); }

  void schedule() override { scheduled_ = true; }

  HHWheelTimer* timer() override { return timer_.get(); }

  bool isInLoopThread() const {
    auto tid = loopThread_.load(std::memory_order_relaxed);
    return tid == std::thread::id() || tid == std::this_thread::get_id();
  }

 private:
  FiberManager* fm_;
  std::atomic<bool> scheduled_{false};
  bool stopRequested_;
  std::atomic<int> remoteScheduleCalled_{0};
  int remoteLoopRun_{0};
  std::atomic<std::thread::id> loopThread_;

  class SimpleTimeoutManager;
  std::unique_ptr<SimpleTimeoutManager> timeoutManager_;
  std::shared_ptr<HHWheelTimer> timer_;

  /* LoopController interface */

  void setFiberManager(FiberManager* fm) override { fm_ = fm; }

  void scheduleThreadSafe() override {
    ++remoteScheduleCalled_;
    scheduled_ = true;
  }

  void runTimeouts();

  friend class FiberManager;
};
} // namespace fibers
} // namespace folly
