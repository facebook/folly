/*
 * Copyright 2017-present Facebook, Inc.
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

#include <exception>
#include <stdexcept>

#include <glog/logging.h>

#include <folly/CPortability.h>

namespace folly {

// Some queue implementations (for example, LifoSemMPMCQueue or
// PriorityLifoSemMPMCQueue) support both blocking (BLOCK) and
// non-blocking (THROW) behaviors.
enum class QueueBehaviorIfFull { THROW, BLOCK };

class FOLLY_EXPORT QueueFullException : public std::runtime_error {
  using std::runtime_error::runtime_error; // Inherit constructors.
};

template <class T>
class BlockingQueue {
 public:
  virtual ~BlockingQueue() = default;
  virtual void add(T item) = 0;
  virtual void addWithPriority(T item, int8_t /* priority */) {
    add(std::move(item));
  }
  virtual uint8_t getNumPriorities() {
    return 1;
  }
  virtual T take() = 0;
  virtual size_t size() = 0;
};

} // namespace folly
