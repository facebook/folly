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

#include <folly/wangle/rx/types.h> // must come first
#include <folly/wangle/rx/Observable.h>

namespace folly { namespace wangle {

template <class T>
class Subscription {
 public:
  Subscription() {}

  Subscription(const Subscription&) = delete;

  Subscription(Subscription&& other) noexcept {
    *this = std::move(other);
  }

  Subscription& operator=(Subscription&& other) noexcept {
    unsubscribe();
    unsubscriber_ = std::move(other.unsubscriber_);
    id_ = other.id_;
    other.unsubscriber_ = nullptr;
    other.id_ = 0;
    return *this;
  }

  ~Subscription() {
    unsubscribe();
  }

 private:
  typedef typename Observable<T>::Unsubscriber Unsubscriber;

  Subscription(std::shared_ptr<Unsubscriber> unsubscriber, uint64_t id)
    : unsubscriber_(std::move(unsubscriber)), id_(id) {
    CHECK(id_ > 0);
  }

  void unsubscribe() {
    if (unsubscriber_ && id_ > 0) {
      unsubscriber_->unsubscribe(id_);
      id_ = 0;
      unsubscriber_ = nullptr;
    }
  }

  std::shared_ptr<Unsubscriber> unsubscriber_;
  uint64_t id_{0};

  friend class Observable<T>;
};

}}
