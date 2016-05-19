/*
 * Copyright 2016 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <folly/fibers/LoopController.h>
#include <folly/io/async/EventBase.h>
#include <atomic>
#include <memory>

namespace folly {
class EventBase;
}

namespace folly {
namespace fibers {

class FiberManager;

class EventBaseLoopController : public LoopController {
 public:
  explicit EventBaseLoopController();
  ~EventBaseLoopController();

  /**
   * Attach EventBase after LoopController was created.
   */
  void attachEventBase(folly::EventBase& eventBase);

  folly::EventBase* getEventBase() {
    return eventBase_;
  }

 private:
  class ControllerCallback : public folly::EventBase::LoopCallback {
   public:
    explicit ControllerCallback(EventBaseLoopController& controller)
        : controller_(controller) {}

    void runLoopCallback() noexcept override {
      controller_.runLoop();
    }

   private:
    EventBaseLoopController& controller_;
  };

  class DestructionCallback : public folly::EventBase::LoopCallback {
   public:
    DestructionCallback() : alive_(new int(42)) {}
    ~DestructionCallback() {
      reset();
    }

    void runLoopCallback() noexcept override {
      reset();
    }

    std::weak_ptr<void> getWeak() {
      return {alive_};
    }

   private:
    void reset() {
      std::weak_ptr<void> aliveWeak(alive_);
      alive_.reset();

      while (!aliveWeak.expired()) {
        // Spin until all operations requiring EventBaseLoopController to be
        // alive are complete.
      }
    }

    std::shared_ptr<void> alive_;
  };

  bool awaitingScheduling_{false};
  folly::EventBase* eventBase_{nullptr};
  ControllerCallback callback_;
  DestructionCallback destructionCallback_;
  FiberManager* fm_{nullptr};
  std::atomic<bool> eventBaseAttached_{false};
  std::weak_ptr<void> aliveWeak_;

  /* LoopController interface */

  void setFiberManager(FiberManager* fm) override;
  void schedule() override;
  void cancel() override;
  void runLoop();
  void scheduleThreadSafe(std::function<bool()> func) override;
  void timedSchedule(std::function<void()> func, TimePoint time) override;

  friend class FiberManager;
};
}
} // folly::fibers

#include "EventBaseLoopController-inl.h"
