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

#pragma once

#include <folly/Executor.h>

namespace folly {
class EventBase;
} // namespace folly

namespace folly {

// An IOExecutor is an executor that operates on at least one EventBase.  One of
// these EventBases should be accessible via getEventBase(). The event base
// returned by a call to getEventBase() is implementation dependent.
//
// Note that IOExecutors don't necessarily loop on the base themselves - for
// instance, EventBase itself is an IOExecutor but doesn't drive itself.
//
// Implementations of IOExecutor are eligible to become the global IO executor,
// returned on every call to getIOExecutor(), via setIOExecutor().
// These functions are declared in GlobalExecutor.h
//
// If getIOExecutor is called and none has been set, a default global
// IOThreadPoolExecutor will be created and returned.
class IOExecutor : public virtual folly::Executor {
 public:
  ~IOExecutor() override = default;

  // Returns a borrowed EventBase. Its lifetime must be coordinated with the
  // implementation's shutdown or resizing.
  virtual folly::EventBase* getEventBase() = 0;

  // Returns an implementation-selected EventBase protected by the token, which
  // may delay shutdown or resizing. This does not retain the IOExecutor or
  // change EventBase thread-affinity requirements. An unavailable EventBase may
  // be represented by an empty token or an exception, as with getEventBase().
  virtual folly::Executor::KeepAlive<folly::EventBase>
  getEventBaseKeepAlive() = 0;
};

} // namespace folly
