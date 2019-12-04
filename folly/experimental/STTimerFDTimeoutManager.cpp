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

#include <folly/experimental/STTimerFDTimeoutManager.h>
#include <folly/io/async/EventUtil.h>

namespace folly {
// STTimerFDTimeoutManager
STTimerFDTimeoutManager::STTimerFDTimeoutManager(folly::EventBase* eventBase)
    : TimerFD(eventBase), eventBase_(eventBase) {}

STTimerFDTimeoutManager::~STTimerFDTimeoutManager() {
  cancel();
  close();
}

void STTimerFDTimeoutManager::setActive(AsyncTimeout* obj, bool active) {
  if (obj) {
    auto* ev = obj->getEvent();
    if (active) {
      event_ref_flags(ev->getEvent()) |= EVLIST_ACTIVE;
    } else {
      event_ref_flags(ev->getEvent()) &= ~EVLIST_ACTIVE;
    }
  }
}

void STTimerFDTimeoutManager::attachTimeoutManager(
    AsyncTimeout* /*unused*/,
    InternalEnum /*unused*/) {}

void STTimerFDTimeoutManager::detachTimeoutManager(AsyncTimeout* obj) {
  cancelTimeout(obj);
}

bool STTimerFDTimeoutManager::scheduleTimeout(
    AsyncTimeout* obj,
    timeout_type timeout) {
  timeout_type_high_res high_res_timeout(timeout);
  return scheduleTimeoutHighRes(obj, high_res_timeout);
}

bool STTimerFDTimeoutManager::scheduleTimeoutHighRes(
    AsyncTimeout* obj,
    timeout_type_high_res timeout) {
  CHECK(obj_ == nullptr || obj_ == obj)
      << "Scheduling multiple timeouts on a single timeout manager is not allowed!";
  // no need to cancel - just reschedule
  obj_ = obj;
  setActive(obj, true);
  schedule(timeout);

  return true;
}

void STTimerFDTimeoutManager::cancelTimeout(AsyncTimeout* obj) {
  if (obj == obj_) {
    setActive(obj, false);
    obj_ = nullptr;
    cancel();
  }
}

void STTimerFDTimeoutManager::bumpHandlingTime() {}

void STTimerFDTimeoutManager::onTimeout() noexcept {
  if (obj_) {
    auto* obj = obj_;
    obj_ = nullptr;
    setActive(obj, false);
    obj->timeoutExpired();
  }
}

} // namespace folly
