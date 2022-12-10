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

#include <folly/io/async/EventBaseManager.h>

namespace folly {

std::atomic<EventBaseManager*> globalManager(nullptr);

EventBaseManager* EventBaseManager::get() {
  EventBaseManager* mgr = globalManager;
  if (mgr) {
    return mgr;
  }

  auto new_mgr = new EventBaseManager;
  bool exchanged = globalManager.compare_exchange_strong(mgr, new_mgr);
  if (!exchanged) {
    delete new_mgr;
    return mgr;
  } else {
    return new_mgr;
  }
}

/*
 * EventBaseManager methods
 */

void EventBaseManager::setEventBase(EventBase* eventBase, bool takeOwnership) {
  auto& info = *localStore_.get();
  if (info) {
    throw std::runtime_error(
        "EventBaseManager: cannot set a new EventBase "
        "for this thread when one already exists");
  }

  info.emplace(eventBase, takeOwnership);
}

void EventBaseManager::clearEventBase() {
  auto& info = *localStore_.get();
  if (info && info->isOwned) {
    // EventBase destructor may invoke user callbacks that rely on
    // getEventBase() returning the current EventBase, so make sure that the
    // info is reset only after the EventBase is destroyed.
    delete info->eventBase;
    info->eventBase = nullptr;
  }
  info.reset();
}

EventBase* EventBaseManager::getEventBase() const {
  auto& info = *localStore_.get();
  if (!info) {
    auto evb = std::make_unique<EventBase>(options_);
    info.emplace(evb.release(), true);

    if (observer_) {
      info->eventBase->setObserver(observer_);
    }
  }

  return info->eventBase;
}

} // namespace folly
