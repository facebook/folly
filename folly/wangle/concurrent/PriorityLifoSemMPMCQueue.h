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
#include <folly/wangle/concurrent/BlockingQueue.h>
#include <folly/LifoSem.h>
#include <folly/MPMCQueue.h>

namespace folly { namespace wangle {

template <class T>
class PriorityLifoSemMPMCQueue : public BlockingQueue<T> {
 public:
  explicit PriorityLifoSemMPMCQueue(uint8_t numPriorities, size_t capacity) {
    queues_.reserve(numPriorities);
    for (int8_t i = 0; i < numPriorities; i++) {
      queues_.push_back(MPMCQueue<T>(capacity));
    }
  }

  uint8_t getNumPriorities() override {
    return queues_.size();
  }

  // Add at medium priority by default
  void add(T item) override {
    addWithPriority(std::move(item), Executor::MID_PRI);
  }

  void addWithPriority(T item, int8_t priority) override {
    int mid = getNumPriorities() / 2;
    size_t queue = priority < 0 ?
                   std::max(0, mid + priority) :
                   std::min(getNumPriorities() - 1, mid + priority);
    CHECK(queue < queues_.size());
    if (!queues_[queue].write(std::move(item))) {
      throw std::runtime_error("LifoSemMPMCQueue full, can't add item");
    }
    sem_.post();
  }

  T take() override {
    T item;
    while (true) {
      for (auto it = queues_.rbegin(); it != queues_.rend(); it++) {
        if (it->read(item)) {
          return item;
        }
      }
      sem_.wait();
    }
  }

  size_t size() override {
    size_t size = 0;
    for (auto& q : queues_) {
      size += q.size();
    }
    return size;
  }

 private:
  LifoSem sem_;
  std::vector<MPMCQueue<T>> queues_;
};

}} // folly::wangle
