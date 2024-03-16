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

#include <cstdint>

#include <boost/intrusive/list.hpp>

namespace folly {

/**
 * Observes the execution of a task. Multiple execution observers can be chained
 * together. As a caveat, execution observers should not remove themselves from
 * the list of observers during execution
 */
class ExecutionObserver
    : public boost::intrusive::list_base_hook<
          boost::intrusive::link_mode<boost::intrusive::auto_unlink>> {
 public:
  enum class CallbackType {
    // Owned by EventBase.
    Event,
    Loop,
    NotificationQueue,
    // Owned by FiberManager.
    Fiber,
  };
  // Constant time size = false to support auto_unlink behavior, options are
  // mutually exclusive
  typedef boost::intrusive::
      list<ExecutionObserver, boost::intrusive::constant_time_size<false>>
          List;

  virtual ~ExecutionObserver() = default;

  /**
   * Called when a task is about to start executing.
   *
   * @param id Unique id for the task which is starting.
   */
  virtual void starting(uintptr_t id, CallbackType callbackType) noexcept = 0;

  /**
   * Called just after a task stops executing.
   *
   * @param id Unique id for the task which stopped.
   */
  virtual void stopped(uintptr_t id, CallbackType callbackType) noexcept = 0;
};

} // namespace folly
