/*
 * Copyright 2014 Facebook, Inc.
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

#include <atomic>
#include <thread>

#include "WangleException.h"
#include "detail/State.h"

namespace folly { namespace wangle {

template <class T>
Promise<T>::Promise() : retrieved_(false), state_(new detail::State<T>())
{}

template <class T>
Promise<T>::Promise(Promise<T>&& other) : state_(nullptr) {
  *this = std::move(other);
}

template <class T>
Promise<T>& Promise<T>::operator=(Promise<T>&& other) {
  std::swap(state_, other.state_);
  std::swap(retrieved_, other.retrieved_);
  return *this;
}

template <class T>
void Promise<T>::throwIfFulfilled() {
  if (!state_)
    throw NoState();
  if (state_->ready())
    throw PromiseAlreadySatisfied();
}

template <class T>
void Promise<T>::throwIfRetrieved() {
  if (retrieved_)
    throw FutureAlreadyRetrieved();
}

template <class T>
Promise<T>::~Promise() {
  detach();
}

template <class T>
void Promise<T>::detach() {
  if (state_) {
    if (!retrieved_)
      state_->detachFuture();
    state_->detachPromise();
    state_ = nullptr;
  }
}

template <class T>
Future<T> Promise<T>::getFuture() {
  throwIfRetrieved();
  retrieved_ = true;
  return Future<T>(state_);
}

template <class T>
template <class E>
void Promise<T>::setException(E const& e) {
  setException(std::make_exception_ptr<E>(e));
}

template <class T>
void Promise<T>::setException(std::exception_ptr const& e) {
  throwIfFulfilled();
  state_->setException(e);
}

template <class T>
void Promise<T>::fulfilTry(Try<T>&& t) {
  throwIfFulfilled();
  state_->fulfil(std::move(t));
}

template <class T>
template <class M>
void Promise<T>::setValue(M&& v) {
  static_assert(!std::is_same<T, void>::value,
                "Use setValue() instead");

  fulfilTry(Try<T>(std::forward<M>(v)));
}

template <class T>
void Promise<T>::setValue() {
  static_assert(std::is_same<T, void>::value,
                "Use setValue(value) instead");

  fulfilTry(Try<void>());
}

template <class T>
template <class F>
void Promise<T>::fulfil(F&& func) {
  throwIfFulfilled();
  fulfilTry(makeTryFunction(std::forward<F>(func)));
}

}}
