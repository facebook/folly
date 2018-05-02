/*
 * Copyright 2018-present Facebook, Inc.
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

namespace folly {
namespace python {

inline AsyncioLoopController::AsyncioLoopController(AsyncioExecutor* executor)
    : executor_(executor) {}

inline AsyncioLoopController::~AsyncioLoopController() {}

inline void AsyncioLoopController::setFiberManager(fibers::FiberManager* fm) {
  fm_ = fm;
}

inline void AsyncioLoopController::schedule() {
  // add() is thread-safe, so this isn't properly optimized for addTask()
  executor_->add([this]() { return runLoop(); });
}

inline void AsyncioLoopController::runLoop() {
  if (fm_->hasTasks()) {
    fm_->loopUntilNoReadyImpl();
  }
}

inline void AsyncioLoopController::scheduleThreadSafe() {
  executor_->add([this]() {
    if (fm_->shouldRunLoopRemote()) {
      return runLoop();
    }
  });
}

inline void AsyncioLoopController::timedSchedule(
    std::function<void()>,
    TimePoint) {
  throw std::logic_error("Time schedule isn't supported by asyncio executor");
}

} // namespace python
} // namespace folly
