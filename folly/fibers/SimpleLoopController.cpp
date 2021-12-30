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

#include <folly/fibers/SimpleLoopController.h>

#include <folly/io/async/TimeoutManager.h>

namespace folly {
namespace fibers {

/**
 * A simple version of TimeoutManager that maintains only a single AsyncTimeout
 * object that is used by HHWheelTimer in SimpleLoopController.
 */
class SimpleLoopController::SimpleTimeoutManager : public TimeoutManager {
 public:
  explicit SimpleTimeoutManager(SimpleLoopController& loopController)
      : loopController_(loopController) {}

  void attachTimeoutManager(
      AsyncTimeout* /* unused */, InternalEnum /* unused */) final {}
  void detachTimeoutManager(AsyncTimeout* /* unused */) final {}

  bool scheduleTimeout(AsyncTimeout* obj, timeout_type timeout) final {
    // Make sure that we don't try to use this manager with two timeouts.
    CHECK(!timeout_ || timeout_->first == obj);
    timeout_.emplace(obj, std::chrono::steady_clock::now() + timeout);
    return true;
  }

  void cancelTimeout(AsyncTimeout* obj) final {
    CHECK(timeout_ && timeout_->first == obj);
    timeout_.reset();
  }

  void bumpHandlingTime() final {}

  bool isInTimeoutManagerThread() final {
    return loopController_.isInLoopThread();
  }

  void runTimeouts() {
    std::chrono::steady_clock::time_point tp = std::chrono::steady_clock::now();
    if (!timeout_ || tp < timeout_->second) {
      return;
    }

    auto* timeout = timeout_->first;
    timeout_.reset();
    timeout->timeoutExpired();
  }

 private:
  SimpleLoopController& loopController_;
  folly::Optional<
      std::pair<AsyncTimeout*, std::chrono::steady_clock::time_point>>
      timeout_;
};

SimpleLoopController::SimpleLoopController()
    : fm_(nullptr),
      stopRequested_(false),
      loopThread_(),
      timeoutManager_(std::make_unique<SimpleTimeoutManager>(*this)),
      timer_(HHWheelTimer::newTimer(timeoutManager_.get())) {}

SimpleLoopController::~SimpleLoopController() {
  scheduled_ = false;
}

void SimpleLoopController::runTimeouts() {
  timeoutManager_->runTimeouts();
}

} // namespace fibers
} // namespace folly
