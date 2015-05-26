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
class LifoSemMPMCQueue : public BlockingQueue<T> {
 public:
  explicit LifoSemMPMCQueue(size_t max_capacity) : queue_(max_capacity) {}

  void add(T item) override {
    if (!queue_.write(std::move(item))) {
      throw std::runtime_error("LifoSemMPMCQueue full, can't add item");
    }
    sem_.post();
  }

  T take() override {
    T item;
    while (!queue_.read(item)) {
      sem_.wait();
    }
    return item;
  }

  size_t capacity() {
    return queue_.capacity();
  }

  size_t size() override {
    return queue_.size();
  }

 private:
  LifoSem sem_;
  MPMCQueue<T> queue_;
};

}} // folly::wangle
