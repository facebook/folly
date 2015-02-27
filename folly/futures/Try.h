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

#include <type_traits>
#include <exception>
#include <algorithm>
#include <folly/ExceptionWrapper.h>
#include <folly/Likely.h>
#include <folly/Memory.h>
#include <folly/futures/Deprecated.h>
#include <folly/futures/FutureException.h>

namespace folly {

/*
 * Try<T> is a wrapper that contains either an instance of T, an exception, or
 * nothing. Its primary use case is as a representation of a Promise or Future's
 * result and so it provides a number of methods that are useful in that
 * context. Exceptions are stored as exception_wrappers so that the user can
 * minimize rethrows if so desired.
 *
 * There is a specialization, Try<void>, which represents either success
 * or an exception.
 */
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
  /*
   * The value type for the Try
   */
  typedef T element_type;

  /*
   * Construct an empty Try
   */
  Try() : contains_(Contains::NOTHING) {}

  /*
   * Construct a Try with a value by copy
   *
   * @param v The value to copy in
   */
  explicit Try(const T& v) : contains_(Contains::VALUE), value_(v) {}

  /*
   * Construct a Try with a value by move
   *
   * @param v The value to move in
   */
  explicit Try(T&& v) : contains_(Contains::VALUE), value_(std::move(v)) {}

  /*
   * Construct a Try with an exception_wrapper
   *
   * @param e The exception_wrapper
   */
  explicit Try(exception_wrapper e)
    : contains_(Contains::EXCEPTION),
      e_(folly::make_unique<exception_wrapper>(std::move(e))) {}

  /*
   * DEPRECATED
   * Construct a Try with an exception_pointer
   *
   * @param ep The exception_pointer. Will be rethrown.
   */
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

  // Move constructor
  Try(Try<T>&& t);
  // Move assigner
  Try& operator=(Try<T>&& t);

  // Non-copyable
  Try(const Try<T>& t) = delete;
  // Non-copyable
  Try& operator=(const Try<T>& t) = delete;

  ~Try();

  /*
   * Get a mutable reference to the contained value. If the Try contains an
   * exception it will be rethrown.
   *
   * @returns mutable reference to the contained value
   */
  T& value();
  /*
   * Get a const reference to the contained value. If the Try contains an
   * exception it will be rethrown.
   *
   * @returns const reference to the contained value
   */
  const T& value() const;

  /*
   * If the Try contains an exception, rethrow it. Otherwise do nothing.
   */
  void throwIfFailed() const;

  /*
   * Const dereference operator. If the Try contains an exception it will be
   * rethrown.
   *
   * @returns const reference to the contained value
   */
  const T& operator*() const { return value(); }
  /*
   * Dereference operator. If the Try contains an exception it will be rethrown.
   *
   * @returns mutable reference to the contained value
   */
  T& operator*() { return value(); }

  /*
   * Const arrow operator. If the Try contains an exception it will be
   * rethrown.
   *
   * @returns const reference to the contained value
   */
  const T* operator->() const { return &value(); }
  /*
   * Arrow operator. If the Try contains an exception it will be rethrown.
   *
   * @returns mutable reference to the contained value
   */
  T* operator->() { return &value(); }

  /*
   * @returns True if the Try contains a value, false otherwise
   */
  bool hasValue() const { return contains_ == Contains::VALUE; }
  /*
   * @returns True if the Try contains an exception, false otherwise
   */
  bool hasException() const { return contains_ == Contains::EXCEPTION; }

  /*
   * @returns True if the Try contains an exception of type Ex, false otherwise
   */
  template <class Ex>
  bool hasException() const {
    return hasException() && e_->is_compatible_with<Ex>();
  }

  exception_wrapper& exception() {
    if (UNLIKELY(!hasException())) {
      throw FutureException("exception(): Try does not contain an exception");
    }
    return *e_;
  }

  const exception_wrapper& exception() const {
    if (UNLIKELY(!hasException())) {
      throw FutureException("exception(): Try does not contain an exception");
    }
    return *e_;
  }

  /*
   * If the Try contains an exception and it is of type Ex, execute func(Ex)
   *
   * @param func a function that takes a single parameter of type const Ex&
   *
   * @returns True if the Try held an Ex and func was executed, false otherwise
   */
  template <class Ex, class F>
  bool withException(F func) const {
    if (!hasException()) {
      return false;
    }
    return e_->with_exception<Ex>(std::move(func));
  }

  template <bool isTry, typename R>
  typename std::enable_if<isTry, R>::type get() {
    return std::forward<R>(*this);
  }

  template <bool isTry, typename R>
  typename std::enable_if<!isTry, R>::type get() {
    return std::forward<R>(value());
  }

 private:
  Contains contains_;
  union {
    T value_;
    std::unique_ptr<exception_wrapper> e_;
  };
};

/*
 * Specialization of Try for void value type. Encapsulates either success or an
 * exception.
 */
template <>
class Try<void> {
 public:
  // Construct a Try holding a successful and void result
  Try() : hasValue_(true) {}

  /*
   * Construct a Try with an exception_wrapper
   *
   * @param e The exception_wrapper
   */
  explicit Try(exception_wrapper e)
    : hasValue_(false),
      e_(folly::make_unique<exception_wrapper>(std::move(e))) {}

  /*
   * DEPRECATED
   * Construct a Try with an exception_pointer
   *
   * @param ep The exception_pointer. Will be rethrown.
   */
  explicit Try(std::exception_ptr ep) DEPRECATED : hasValue_(false) {
    try {
      std::rethrow_exception(ep);
    } catch (const std::exception& e) {
      e_ = folly::make_unique<exception_wrapper>(std::current_exception(), e);
    } catch (...) {
      e_ = folly::make_unique<exception_wrapper>(std::current_exception());
    }
  }

  // Copy assigner
  Try& operator=(const Try<void>& t) {
    hasValue_ = t.hasValue_;
    if (t.e_) {
      e_ = folly::make_unique<exception_wrapper>(*t.e_);
    }
    return *this;
  }
  // Copy constructor
  Try(const Try<void>& t) {
    *this = t;
  }

  // If the Try contains an exception, throws it
  void value() const { throwIfFailed(); }
  // Dereference operator. If the Try contains an exception, throws it
  void operator*() const { return value(); }

  // If the Try contains an exception, throws it
  inline void throwIfFailed() const;

  // @returns False if the Try contains an exception, true otherwise
  bool hasValue() const { return hasValue_; }
  // @returns True if the Try contains an exception, false otherwise
  bool hasException() const { return !hasValue_; }

  // @returns True if the Try contains an exception of type Ex, false otherwise
  template <class Ex>
  bool hasException() const {
    return hasException() && e_->is_compatible_with<Ex>();
  }

  /*
   * @throws FutureException if the Try doesn't contain an exception
   *
   * @returns mutable reference to the exception contained by this Try
   */
  exception_wrapper& exception() {
    if (UNLIKELY(!hasException())) {
      throw FutureException("exception(): Try does not contain an exception");
    }
    return *e_;
  }

  const exception_wrapper& exception() const {
    if (UNLIKELY(!hasException())) {
      throw FutureException("exception(): Try does not contain an exception");
    }
    return *e_;
  }

  /*
   * If the Try contains an exception and it is of type Ex, execute func(Ex)
   *
   * @param func a function that takes a single parameter of type const Ex&
   *
   * @returns True if the Try held an Ex and func was executed, false otherwise
   */
  template <class Ex, class F>
  bool withException(F func) const {
    if (!hasException()) {
      return false;
    }
    return e_->with_exception<Ex>(std::move(func));
  }

  template <bool, typename R>
  R get() {
    return std::forward<R>(*this);
  }

 private:
  bool hasValue_;
  std::unique_ptr<exception_wrapper> e_{nullptr};
};

/*
 * Extracts value from try and returns it. Throws if try contained an exception.
 *
 * @param t Try to extract value from
 *
 * @returns value contained in t
 */
template <typename T>
T moveFromTry(Try<T>&& t);

/*
 * Throws if try contained an exception.
 *
 * @param t Try to move from
 */
void moveFromTry(Try<void>&& t);

/*
 * @param f a function to execute and capture the result of (value or exception)
 *
 * @returns Try holding the result of f
 */
template <typename F>
typename std::enable_if<
  !std::is_same<typename std::result_of<F()>::type, void>::value,
  Try<typename std::result_of<F()>::type>>::type
makeTryFunction(F&& f);

/*
 * Specialization of makeTryFunction for void
 *
 * @param f a function to execute and capture the result of
 *
 * @returns Try<void> holding the result of f
 */
template <typename F>
typename std::enable_if<
  std::is_same<typename std::result_of<F()>::type, void>::value,
  Try<void>>::type
makeTryFunction(F&& f);

} // folly

#include <folly/futures/Try-inl.h>
