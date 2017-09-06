/*
 * Copyright 2017 Facebook, Inc.
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

#include <folly/LifoSem.h>
#include <folly/Synchronized.h>
#include <folly/executors/BlockingQueue.h>
#include <queue>

namespace folly {

// Warning: this is effectively just a std::deque wrapped in a single mutex
// We are aiming to add a more performant concurrent unbounded queue in the
// future, but this class is available if you must have an unbounded queue
// and can tolerate any contention.
template <class T>
class UnboundedBlockingQueue : public BlockingQueue<T> {
 public:
  virtual ~UnboundedBlockingQueue() {}

  void add(T item) override {
    queue_.wlock()->push(std::move(item));
    sem_.post();
  }

  T take() override {
    while (true) {
      {
        auto ulockedQueue = queue_.ulock();
        if (!ulockedQueue->empty()) {
          auto wlockedQueue = ulockedQueue.moveFromUpgradeToWrite();
          T item = std::move(wlockedQueue->front());
          wlockedQueue->pop();
          return item;
        }
      }
      sem_.wait();
    }
  }

  size_t size() override {
    return queue_.rlock()->size();
  }

 private:
  LifoSem sem_;
  Synchronized<std::queue<T>> queue_;
};

} // namespace folly
