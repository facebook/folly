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

#include <type_traits>
#include <exception>
#include <algorithm>
#include <folly/ExceptionWrapper.h>
#include <folly/Likely.h>
#include <folly/Memory.h>
#include <folly/wangle/futures/Deprecated.h>
#include <folly/wangle/futures/WangleException.h>

namespace folly { namespace wangle {

template <class T>
class Try {
  static_assert(!std::is_reference<T>::value,
                "Try may not be used with reference types");

  enum class Contains {
    VALUE,
    EXCEPTION,
    NOTHING,
  };

 public:
  typedef T element_type;

  Try() : contains_(Contains::NOTHING) {}
  explicit Try(const T& v) : contains_(Contains::VALUE), value_(v) {}
  explicit Try(T&& v) : contains_(Contains::VALUE), value_(std::move(v)) {}
  explicit Try(exception_wrapper e)
    : contains_(Contains::EXCEPTION),
      e_(folly::make_unique<exception_wrapper>(std::move(e))) {}
  explicit Try(std::exception_ptr ep) DEPRECATED
    : contains_(Contains::EXCEPTION) {
    try {
      std::rethrow_exception(ep);
    } catch (const std::exception& e) {
      e_ = folly::make_unique<exception_wrapper>(std::current_exception(), e);
    } catch (...) {
      e_ = folly::make_unique<exception_wrapper>(std::current_exception());
    }
  }

  // move
  Try(Try<T>&& t);
  Try& operator=(Try<T>&& t);

  // no copy
  Try(const Try<T>& t) = delete;
  Try& operator=(const Try<T>& t) = delete;

  ~Try();

  T& value();
  const T& value() const;

  void throwIfFailed() const;

  const T& operator*() const { return value(); }
        T& operator*()       { return value(); }

  const T* operator->() const { return &value(); }
        T* operator->()       { return &value(); }

  bool hasValue() const { return contains_ == Contains::VALUE; }
  bool hasException() const { return contains_ == Contains::EXCEPTION; }

  template <class Ex>
  bool hasException() const {
    return hasException() && e_->is_compatible_with<Ex>();
  }

  exception_wrapper& exception() {
    if (UNLIKELY(!hasException())) {
      throw WangleException("exception(): Try does not contain an exception");
    }
    return *e_;
  }

  template <class Ex, class F>
  bool withException(F func) const {
    if (!hasException()) {
      return false;
    }
    return e_->with_exception<Ex>(std::move(func));
  }

 private:
  Contains contains_;
  union {
    T value_;
    std::unique_ptr<exception_wrapper> e_;
  };
};

template <>
class Try<void> {
 public:
  Try() : hasValue_(true) {}
  explicit Try(exception_wrapper e)
    : hasValue_(false),
      e_(folly::make_unique<exception_wrapper>(std::move(e))) {}
  explicit Try(std::exception_ptr ep) DEPRECATED : hasValue_(false) {
    try {
      std::rethrow_exception(ep);
    } catch (const std::exception& e) {
      e_ = folly::make_unique<exception_wrapper>(std::current_exception(), e);
    } catch (...) {
      e_ = folly::make_unique<exception_wrapper>(std::current_exception());
    }
  }

  Try& operator=(const Try<void>& t) {
    hasValue_ = t.hasValue_;
    if (t.e_) {
      e_ = folly::make_unique<exception_wrapper>(*t.e_);
    }
    return *this;
  }
  Try(const Try<void>& t) {
    *this = t;
  }

  void value() const { throwIfFailed(); }
  void operator*() const { return value(); }

  inline void throwIfFailed() const;

  bool hasValue() const { return hasValue_; }
  bool hasException() const { return !hasValue_; }

  template <class Ex>
  bool hasException() const {
    return hasException() && e_->is_compatible_with<Ex>();
  }

  exception_wrapper& exception() {
    if (UNLIKELY(!hasException())) {
      throw WangleException("exception(): Try does not contain an exception");
    }
    return *e_;
  }

  template <class Ex, class F>
  bool withException(F func) const {
    if (!hasException()) {
      return false;
    }
    return e_->with_exception<Ex>(std::move(func));
  }

 private:
  bool hasValue_;
  std::unique_ptr<exception_wrapper> e_{nullptr};
};

/**
 * Extracts value from try and returns it. Throws if try contained an exception.
 */
template <typename T>
T moveFromTry(wangle::Try<T>&& t);

/**
 * Throws if try contained an exception.
 */
void moveFromTry(wangle::Try<void>&& t);

/**
 * Constructs Try based on the result of execution of function f (e.g. result
 * or exception).
 */
template <typename F>
typename std::enable_if<
  !std::is_same<typename std::result_of<F()>::type, void>::value,
  Try<typename std::result_of<F()>::type>>::type
makeTryFunction(F&& f);

/**
 * makeTryFunction specialization for void functions.
 */
template <typename F>
typename std::enable_if<
  std::is_same<typename std::result_of<F()>::type, void>::value,
  Try<void>>::type
makeTryFunction(F&& f);


}}

#include <folly/wangle/futures/Try-inl.h>
