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

#include <stdexcept>

#include "WangleException.h"

namespace folly { namespace wangle {

template <class T>
Try<T>::Try(Try<T>&& t) : contains_(t.contains_) {
  if (contains_ == Contains::VALUE) {
    new (&value_)T(std::move(t.value_));
  } else if (contains_ == Contains::EXCEPTION) {
    new (&e_)std::exception_ptr(t.e_);
  }
}

template <class T>
Try<T>& Try<T>::operator=(Try<T>&& t) {
  this->~Try();
  contains_ = t.contains_;
  if (contains_ == Contains::VALUE) {
    new (&value_)T(std::move(t.value_));
  } else if (contains_ == Contains::EXCEPTION) {
    new (&e_)std::exception_ptr(t.e_);
  }
  return *this;
}

template <class T>
Try<T>::~Try() {
  if (contains_ == Contains::VALUE) {
    value_.~T();
  } else if (contains_ == Contains::EXCEPTION) {
    e_.~exception_ptr();
  }
}

template <class T>
T& Try<T>::value() {
  throwIfFailed();
  return value_;
}

template <class T>
const T& Try<T>::value() const {
  throwIfFailed();
  return value_;
}

template <class T>
void Try<T>::throwIfFailed() const {
  if (contains_ != Contains::VALUE) {
    if (contains_ == Contains::EXCEPTION) {
      std::rethrow_exception(e_);
    } else {
      throw UsingUninitializedTry();
    }
  }
}

void Try<void>::throwIfFailed() const {
  if (!hasValue_) {
    std::rethrow_exception(e_);
  }
}

template <typename T>
inline T moveFromTry(wangle::Try<T>&& t) {
  return std::move(t.value());
}

inline void moveFromTry(wangle::Try<void>&& t) {
  return t.value();
}

template <typename F>
typename std::enable_if<
  !std::is_same<typename std::result_of<F()>::type, void>::value,
  Try<typename std::result_of<F()>::type>>::type
makeTryFunction(F&& f) {
  typedef typename std::result_of<F()>::type ResultType;
  try {
    auto value = f();
    return Try<ResultType>(std::move(value));
  } catch (...) {
    return Try<ResultType>(std::current_exception());
  }
}

template <typename F>
typename std::enable_if<
  std::is_same<typename std::result_of<F()>::type, void>::value,
  Try<void>>::type
makeTryFunction(F&& f) {
  try {
    f();
    return Try<void>();
  } catch (...) {
    return Try<void>(std::current_exception());
  }
}

}}
