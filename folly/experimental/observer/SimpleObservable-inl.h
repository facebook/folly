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

#include <folly/experimental/observer/Observable.h>

namespace folly {
namespace observer {

template <typename T>
template <typename, typename>
SimpleObservable<T>::SimpleObservable()
    : context_(std::make_shared<Context>(std::make_shared<T>())) {}

template <typename T>
SimpleObservable<T>::SimpleObservable(T value)
    : SimpleObservable(std::make_shared<const T>(std::move(value))) {}

template <typename T>
SimpleObservable<T>::SimpleObservable(std::shared_ptr<const T> value)
    : context_(std::make_shared<Context>(std::move(value))) {}

template <typename T>
void SimpleObservable<T>::setValue(T value) {
  setValue(std::make_shared<const T>(std::move(value)));
}

template <typename T>
void SimpleObservable<T>::setValue(std::shared_ptr<const T> value) {
  context_->value_.swap(value);

  context_->callback_.withWLock([](folly::Function<void()>& callback) {
    if (callback) {
      callback();
    }
  });
}

template <typename T>
SimpleObservable<T>::Context::Context(std::shared_ptr<const T> value)
    : value_{std::move(value)} {}

template <typename T>
struct SimpleObservable<T>::Wrapper {
  using element_type = T;

  std::shared_ptr<Context> context;

  std::shared_ptr<const T> get() { return context->value_.copy(); }

  void subscribe(folly::Function<void()> callback) {
    context->callback_.swap(callback);
  }

  void unsubscribe() {
    Function<void()> empty;
    context->callback_.swap(empty);
  }
};

template <typename T>
auto SimpleObservable<T>::getObserver() const {
  return observer_.try_emplace_with([&]() {
    SimpleObservable<T>::Wrapper wrapper;
    wrapper.context = context_;
    ObserverCreator<SimpleObservable<T>::Wrapper> creator(std::move(wrapper));
    return unwrap(std::move(creator).getObserver());
  });
}

} // namespace observer
} // namespace folly
