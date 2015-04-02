/*
 * Copyright 2015 Facebook, Inc.
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

#include <memory>
#include <folly/experimental/fibers/LoopController.h>

namespace folly {
class EventBase;
}

namespace folly { namespace fibers {

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
  class ControllerCallback;

  bool awaitingScheduling_{false};
  folly::EventBase* eventBase_{nullptr};
  std::unique_ptr<ControllerCallback> callback_;
  FiberManager* fm_{nullptr};
  std::atomic<bool> eventBaseAttached_{false};

  /* LoopController interface */

  void setFiberManager(FiberManager* fm) override;
  void schedule() override;
  void cancel() override;
  void runLoop();
  void scheduleThreadSafe() override;
  void timedSchedule(std::function<void()> func, TimePoint time) override;

  friend class FiberManager;
};

}}  // folly::fibers

#include "EventBaseLoopController-inl.h"
