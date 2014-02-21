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
#include "detail.h"

namespace folly { namespace wangle {

template <class T>
Promise<T>::Promise() : retrieved_(false), obj_(new detail::FutureObject<T>())
{}

template <class T>
Promise<T>::Promise(Promise<T>&& other) :
retrieved_(other.retrieved_), obj_(other.obj_) {
  other.obj_ = nullptr;
}

template <class T>
Promise<T>& Promise<T>::operator=(Promise<T>&& other) {
  std::swap(obj_, other.obj_);
  std::swap(retrieved_, other.retrieved_);
  return *this;
}

template <class T>
void Promise<T>::throwIfFulfilled() {
  if (!obj_)
    throw PromiseAlreadySatisfied();
}

template <class T>
void Promise<T>::throwIfRetrieved() {
  if (retrieved_)
    throw FutureAlreadyRetrieved();
}

template <class T>
Promise<T>::~Promise() {
  if (obj_) {
    setException(BrokenPromise());
  }
}

template <class T>
Future<T> Promise<T>::getFuture() {
  throwIfRetrieved();
  throwIfFulfilled();
  retrieved_ = true;
  return Future<T>(obj_);
}

template <class T>
template <class E>
void Promise<T>::setException(E const& e) {
  throwIfFulfilled();
  setException(std::make_exception_ptr<E>(e));
}

template <class T>
void Promise<T>::setException(std::exception_ptr const& e) {
  throwIfFulfilled();
  obj_->setException(e);
  if (!retrieved_) {
    delete obj_;
  }
  obj_ = nullptr;
}

template <class T>
void Promise<T>::fulfilTry(Try<T>&& t) {
  throwIfFulfilled();
  obj_->fulfil(std::move(t));
  if (!retrieved_) {
    delete obj_;
  }
  obj_ = nullptr;
}

template <class T>
template <class M>
void Promise<T>::setValue(M&& v) {
  static_assert(!std::is_same<T, void>::value,
                "Use setValue() instead");

  throwIfFulfilled();
  obj_->fulfil(Try<T>(std::forward<M>(v)));
  if (!retrieved_) {
    delete obj_;
  }
  obj_ = nullptr;
}

template <class T>
void Promise<T>::setValue() {
  static_assert(std::is_same<T, void>::value,
                "Use setValue(value) instead");

  throwIfFulfilled();
  obj_->fulfil(Try<void>());
  if (!retrieved_) {
    delete obj_;
  }
  obj_ = nullptr;
}

template <class T>
template <class F>
void Promise<T>::fulfil(const F& func) {
  fulfilHelper(func);
}

template <class T>
template <class F>
typename std::enable_if<
  std::is_convertible<typename std::result_of<F()>::type, T>::value &&
  !std::is_same<T, void>::value>::type
inline Promise<T>::fulfilHelper(const F& func) {
  throwIfFulfilled();
  try {
    setValue(func());
  } catch (...) {
    setException(std::current_exception());
  }
}

template <class T>
template <class F>
typename std::enable_if<
  std::is_same<typename std::result_of<F()>::type, void>::value &&
  std::is_same<T, void>::value>::type
inline Promise<T>::fulfilHelper(const F& func) {
  throwIfFulfilled();
  try {
    func();
    setValue();
  } catch (...) {
    setException(std::current_exception());
  }
}

}}
