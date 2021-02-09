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

#include <stdint.h>
#include <memory>
#include <string>

#include <folly/Portability.h>

namespace folly {

class RequestContext;

class QueueObserver {
 public:
  virtual ~QueueObserver() {}

  virtual intptr_t onEnqueued(const RequestContext*) = 0;
  virtual void onDequeued(intptr_t) = 0;
};

class QueueObserverFactory {
 public:
  virtual ~QueueObserverFactory() {}
  virtual std::unique_ptr<QueueObserver> create(int8_t pri) = 0;

  static std::unique_ptr<QueueObserverFactory> make(
      const std::string& context, size_t numPriorities);
};

using MakeQueueObserverFactory =
    std::unique_ptr<QueueObserverFactory>(const std::string&, size_t);
#if FOLLY_HAVE_WEAK_SYMBOLS
FOLLY_ATTR_WEAK MakeQueueObserverFactory make_queue_observer_factory;
#else
constexpr MakeQueueObserverFactory* make_queue_observer_factory = nullptr;
#endif

} // namespace folly
