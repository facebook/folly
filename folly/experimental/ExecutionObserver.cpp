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

#include <folly/experimental/ExecutionObserver.h>

#include <folly/tracing/StaticTracepoint.h>

namespace folly {

ExecutionObserverScopeGuard::ExecutionObserverScopeGuard(
    folly::ExecutionObserver::List* observerList,
    void* id,
    folly::ExecutionObserver::CallbackType callbackType)
    : observerList_(observerList),
      id_{reinterpret_cast<uintptr_t>(id)},
      callbackType_(callbackType) {
  FOLLY_SDT(
      folly,
      execution_observer_callbacks_starting,
      id_,
      static_cast<int>(callbackType_));
  for (auto& observer : *observerList_) {
    observer.starting(id_, callbackType_);
  }
}

ExecutionObserverScopeGuard::~ExecutionObserverScopeGuard() {
  for (auto& observer : *observerList_) {
    observer.stopped(id_, callbackType_);
  }

  FOLLY_SDT(
      folly,
      execution_observer_callbacks_stopped,
      id_,
      static_cast<int>(callbackType_));
}

} // namespace folly
