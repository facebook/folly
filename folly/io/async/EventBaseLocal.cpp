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

#include <folly/io/async/EventBaseLocal.h>

#include <atomic>
#include <thread>

#include <folly/MapUtil.h>
#include <folly/Memory.h>

namespace folly {
namespace detail {

EventBaseLocalBase::~EventBaseLocalBase() {
  auto locked = eventBases_.rlock();
  for (auto* evb : *locked) {
    evb->runInEventBaseThread([this, evb, key = key_] {
      evb->localStorage_.erase(key);
      evb->localStorageToDtor_.erase(this);
    });
  }
}

void* EventBaseLocalBase::getVoid(EventBase& evb) {
  evb.dcheckIsInEventBaseThread();

  auto ptr = folly::get_ptr(evb.localStorage_, key_);
  return ptr ? ptr->get() : nullptr;
}

void EventBaseLocalBase::erase(EventBase& evb) {
  evb.dcheckIsInEventBaseThread();

  evb.localStorage_.erase(key_);
  evb.localStorageToDtor_.erase(this);

  eventBases_.wlock()->erase(&evb);
}

void EventBaseLocalBase::onEventBaseDestruction(EventBase& evb) {
  evb.dcheckIsInEventBaseThread();

  eventBases_.wlock()->erase(&evb);
}

void EventBaseLocalBase::setVoid(
    EventBase& evb, void* ptr, void (*dtor)(void*)) {
  // construct the unique-ptr eagerly, just in case anything between this and
  // the emplace below could throw
  auto erased = erased_unique_ptr{ptr, dtor};

  evb.dcheckIsInEventBaseThread();

  auto alreadyExists = evb.localStorage_.find(key_) != evb.localStorage_.end();

  evb.localStorage_.emplace(key_, std::move(erased));

  if (!alreadyExists) {
    eventBases_.wlock()->insert(&evb);
    evb.localStorageToDtor_.insert(this);
  }
}

std::atomic<std::size_t> EventBaseLocalBase::keyCounter_{0};
} // namespace detail
} // namespace folly
