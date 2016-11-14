/*
 * Copyright 2016 Facebook, Inc.
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements. See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership. The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
#pragma once

#include <chrono>
#include <stdint.h>

namespace folly {

class AsyncTimeout;

/**
 * Base interface to be implemented by all classes expecting to manage
 * timeouts. AsyncTimeout will use implementations of this interface
 * to schedule/cancel timeouts.
 */
class TimeoutManager {
 public:
  typedef std::chrono::milliseconds timeout_type;

  enum class InternalEnum {
    INTERNAL,
    NORMAL
  };

  virtual ~TimeoutManager() = default;

  /**
   * Attaches/detaches TimeoutManager to AsyncTimeout
   */
  virtual void attachTimeoutManager(AsyncTimeout* obj,
                                    InternalEnum internal) = 0;
  virtual void detachTimeoutManager(AsyncTimeout* obj) = 0;

  /**
   * Schedules AsyncTimeout to fire after `timeout` milliseconds
   */
  virtual bool scheduleTimeout(AsyncTimeout* obj,
                               timeout_type timeout) = 0;

  /**
   * Cancels the AsyncTimeout, if scheduled
   */
  virtual void cancelTimeout(AsyncTimeout* obj) = 0;

  /**
   * This is used to mark the beginning of a new loop cycle by the
   * first handler fired within that cycle.
   */
  virtual void bumpHandlingTime() = 0;

  /**
   * Helper method to know whether we are running in the timeout manager
   * thread
   */
  virtual bool isInTimeoutManagerThread() = 0;
};

} // folly
