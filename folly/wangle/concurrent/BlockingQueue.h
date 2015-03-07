/*
 * Copyright 2015 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <glog/logging.h>

namespace folly { namespace wangle {

template <class T>
class BlockingQueue {
 public:
  virtual ~BlockingQueue() {}
  virtual void add(T item) = 0;
  virtual void addWithPriority(T item, uint32_t priority) {
    LOG_FIRST_N(WARNING, 1) <<
      "add(item, priority) called on a non-priority queue";
    add(std::move(item));
  }
  virtual uint32_t getNumPriorities() {
    LOG_FIRST_N(WARNING, 1) <<
      "getNumPriorities() called on a non-priority queue";
    return 1;
  }
  virtual T take() = 0;
  virtual size_t size() = 0;
};

}} // folly::wangle
