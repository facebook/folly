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
  // Remove self from all registered EventBase instances.
  // Notice that we could be racing with EventBase dtor similarly
  // deregistering itself from all registered EventBaseLocal instances. Because
  // both sides need to acquire two locks, but in inverse order, we retry if
  // inner lock acquisition fails to prevent lock inversion deadlock.
  while (true) {
    auto locked = eventBases_.wlock();
    if (locked->empty()) {
      break;
    }
    auto* evb = *locked->begin();
    if (evb->tryDeregister(*this)) {
      locked->erase(evb);
    }
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
  evb.localStorageToDtor_.wlock()->erase(this);

  eventBases_.wlock()->erase(&evb);
}

bool EventBaseLocalBase::tryDeregister(EventBase& evb) {
  evb.dcheckIsInEventBaseThread();

  if (auto locked = eventBases_.tryWLock()) {
    locked->erase(&evb);
    return true;
  }
  return false;
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
    evb.localStorageToDtor_.wlock()->insert(this);
  }
}

std::atomic<std::size_t> EventBaseLocalBase::keyCounter_{0};
} // namespace detail
} // namespace folly
