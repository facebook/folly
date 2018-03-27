/*
 * Copyright 2014-present Facebook, Inc.
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

#include <folly/ExceptionWrapper.h>
#include <folly/Likely.h>
#include <folly/Memory.h>
#include <folly/Portability.h>
#include <folly/Unit.h>
#include <folly/Utility.h>
#include <exception>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace folly {

class FOLLY_EXPORT TryException : public std::logic_error {
 public:
  using std::logic_error::logic_error;
};

class FOLLY_EXPORT UsingUninitializedTry : public TryException {
 public:
  UsingUninitializedTry() : TryException("Using uninitialized try") {}
};

namespace try_detail {
[[noreturn]] void throwTryDoesNotContainException();
[[noreturn]] void throwUsingUninitializedTry();
} // namespace try_detail

/*
 * Try<T> is a wrapper that contains either an instance of T, an exception, or
 * nothing. Exceptions are stored as exception_wrappers so that the user can
 * minimize rethrows if so desired.
 *
 * To represent success or a captured exception, use Try<Unit>.
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

  template <typename... Args>
  explicit Try(in_place_t, Args&&... args) noexcept(
      noexcept(::new (nullptr) T(std::declval<Args&&>()...)))
      : contains_(Contains::VALUE), value_(std::forward<Args>(args)...) {}

  /// Implicit conversion from Try<void> to Try<Unit>
  template <class T2 = T>
  /* implicit */
  Try(typename std::enable_if<std::is_same<Unit, T2>::value,
                              Try<void> const&>::type t);

  /*
   * Construct a Try with an exception_wrapper
   *
   * @param e The exception_wrapper
   */
  explicit Try(exception_wrapper e)
      : contains_(Contains::EXCEPTION), e_(std::move(e)) {}

  /*
   * DEPRECATED
   * Construct a Try with an exception_pointer
   *
   * @param ep The exception_pointer. Will be rethrown.
   */
  [[deprecated("use Try(exception_wrapper)")]]
  explicit Try(std::exception_ptr ep)
      : contains_(Contains::EXCEPTION),
        e_(exception_wrapper::from_exception_ptr(ep)) {}

  // Move constructor
  Try(Try<T>&& t) noexcept;
  // Move assigner
  Try& operator=(Try<T>&& t) noexcept;

  // Copy constructor
  Try(const Try& t);
  // Copy assigner
  Try& operator=(const Try& t);

  ~Try();

  /*
   * Get a mutable reference to the contained value. If the Try contains an
   * exception it will be rethrown.
   *
   * @returns mutable reference to the contained value
   */
  T& value() &;
  /*
   * Get a rvalue reference to the contained value. If the Try contains an
   * exception it will be rethrown.
   *
   * @returns rvalue reference to the contained value
   */
  T&& value() &&;
  /*
   * Get a const reference to the contained value. If the Try contains an
   * exception it will be rethrown.
   *
   * @returns const reference to the contained value
   */
  const T& value() const &;
  /*
   * Get a const rvalue reference to the contained value. If the Try contains an
   * exception it will be rethrown.
   *
   * @returns const rvalue reference to the contained value
   */
  const T&& value() const &&;

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
  const T& operator*() const & {
    return value();
  }
  /*
   * Dereference operator. If the Try contains an exception it will be rethrown.
   *
   * @returns mutable reference to the contained value
   */
  T& operator*() & {
    return value();
  }
  /*
   * Mutable rvalue dereference operator.  If the Try contains an exception it
   * will be rethrown.
   *
   * @returns rvalue reference to the contained value
   */
  T&& operator*() && {
    return std::move(value());
  }
  /*
   * Const rvalue dereference operator.  If the Try contains an exception it
   * will be rethrown.
   *
   * @returns rvalue reference to the contained value
   */
  const T&& operator*() const && {
    return std::move(value());
  }

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
    return hasException() && e_.is_compatible_with<Ex>();
  }

  exception_wrapper& exception() & {
    if (!hasException()) {
      try_detail::throwTryDoesNotContainException();
    }
    return e_;
  }

  exception_wrapper&& exception() && {
    if (!hasException()) {
      try_detail::throwTryDoesNotContainException();
    }
    return std::move(e_);
  }

  const exception_wrapper& exception() const & {
    if (!hasException()) {
      try_detail::throwTryDoesNotContainException();
    }
    return e_;
  }

  const exception_wrapper&& exception() const && {
    if (!hasException()) {
      try_detail::throwTryDoesNotContainException();
    }
    return std::move(e_);
  }

  /*
   * @returns a pointer to the `std::exception` held by `*this`, if one is held;
   *          otherwise, returns `nullptr`.
   */
  std::exception* tryGetExceptionObject() {
    return hasException() ? e_.get_exception() : nullptr;
  }
  std::exception const* tryGetExceptionObject() const {
    return hasException() ? e_.get_exception() : nullptr;
  }

  /*
   * @returns a pointer to the `Ex` held by `*this`, if it holds an object whose
   *          type `From` permits `std::is_convertible<From*, Ex*>`; otherwise,
   *          returns `nullptr`.
   */
  template <class E>
  E* tryGetExceptionObject() {
    return hasException() ? e_.get_exception<E>() : nullptr;
  }
  template <class E>
  E const* tryGetExceptionObject() const {
    return hasException() ? e_.get_exception<E>() : nullptr;
  }

  /*
   * If the Try contains an exception and it is of type Ex, execute func(Ex)
   *
   * @param func a function that takes a single parameter of type const Ex&
   *
   * @returns True if the Try held an Ex and func was executed, false otherwise
   */
  template <class Ex, class F>
  bool withException(F func) {
    if (!hasException()) {
      return false;
    }
    return e_.with_exception<Ex>(std::move(func));
  }
  template <class Ex, class F>
  bool withException(F func) const {
    if (!hasException()) {
      return false;
    }
    return e_.with_exception<Ex>(std::move(func));
  }

  /*
   * If the Try contains an exception and it is of type compatible with Ex as
   * deduced from the first parameter of func, execute func(Ex)
   *
   * @param func a function that takes a single parameter of type const Ex&
   *
   * @returns True if the Try held an Ex and func was executed, false otherwise
   */
  template <class F>
  bool withException(F func) {
    if (!hasException()) {
      return false;
    }
    return e_.with_exception(std::move(func));
  }
  template <class F>
  bool withException(F func) const {
    if (!hasException()) {
      return false;
    }
    return e_.with_exception(std::move(func));
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
    exception_wrapper e_;
  };
};

/*
 * Specialization of Try for void value type. Encapsulates either success or an
 * exception.
 */
template <>
class Try<void> {
 public:
  /*
   * The value type for the Try
   */
  typedef void element_type;

  // Construct a Try holding a successful and void result
  Try() : hasValue_(true) {}

  /*
   * Construct a Try with an exception_wrapper
   *
   * @param e The exception_wrapper
   */
  explicit Try(exception_wrapper e) : hasValue_(false), e_(std::move(e)) {}

  /*
   * DEPRECATED
   * Construct a Try with an exception_pointer
   *
   * @param ep The exception_pointer. Will be rethrown.
   */
  [[deprecated("use Try(exception_wrapper)")]]
  explicit Try(std::exception_ptr ep)
      : hasValue_(false), e_(exception_wrapper::from_exception_ptr(ep)) {}

  // Copy assigner
  Try& operator=(const Try<void>& t) {
    hasValue_ = t.hasValue_;
    e_ = t.e_;
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
    return hasException() && e_.is_compatible_with<Ex>();
  }

  /*
   * @throws TryException if the Try doesn't contain an exception
   *
   * @returns mutable reference to the exception contained by this Try
   */
  exception_wrapper& exception() & {
    if (!hasException()) {
      try_detail::throwTryDoesNotContainException();
    }
    return e_;
  }

  exception_wrapper&& exception() && {
    if (!hasException()) {
      try_detail::throwTryDoesNotContainException();
    }
    return std::move(e_);
  }

  const exception_wrapper& exception() const & {
    if (!hasException()) {
      try_detail::throwTryDoesNotContainException();
    }
    return e_;
  }

  const exception_wrapper&& exception() const && {
    if (!hasException()) {
      try_detail::throwTryDoesNotContainException();
    }
    return std::move(e_);
  }

  /*
   * @returns a pointer to the `std::exception` held by `*this`, if one is held;
   *          otherwise, returns `nullptr`.
   */
  std::exception* tryGetExceptionObject() {
    return hasException() ? e_.get_exception() : nullptr;
  }
  std::exception const* tryGetExceptionObject() const {
    return hasException() ? e_.get_exception() : nullptr;
  }

  /*
   * @returns a pointer to the `Ex` held by `*this`, if it holds an object whose
   *          type `From` permits `std::is_convertible<From*, Ex*>`; otherwise,
   *          returns `nullptr`.
   */
  template <class E>
  E* tryGetExceptionObject() {
    return hasException() ? e_.get_exception<E>() : nullptr;
  }
  template <class E>
  E const* tryGetExceptionObject() const {
    return hasException() ? e_.get_exception<E>() : nullptr;
  }

  /*
   * If the Try contains an exception and it is of type Ex, execute func(Ex)
   *
   * @param func a function that takes a single parameter of type const Ex&
   *
   * @returns True if the Try held an Ex and func was executed, false otherwise
   */
  template <class Ex, class F>
  bool withException(F func) {
    if (!hasException()) {
      return false;
    }
    return e_.with_exception<Ex>(std::move(func));
  }
  template <class Ex, class F>
  bool withException(F func) const {
    if (!hasException()) {
      return false;
    }
    return e_.with_exception<Ex>(std::move(func));
  }

  /*
   * If the Try contains an exception and it is of type compatible with Ex as
   * deduced from the first parameter of func, execute func(Ex)
   *
   * @param func a function that takes a single parameter of type const Ex&
   *
   * @returns True if the Try held an Ex and func was executed, false otherwise
   */
  template <class F>
  bool withException(F func) {
    if (!hasException()) {
      return false;
    }
    return e_.with_exception(std::move(func));
  }
  template <class F>
  bool withException(F func) const {
    if (!hasException()) {
      return false;
    }
    return e_.with_exception(std::move(func));
  }

  template <bool, typename R>
  R get() {
    return std::forward<R>(*this);
  }

 private:
  bool hasValue_;
  exception_wrapper e_;
};

/*
 * @param f a function to execute and capture the result of (value or exception)
 *
 * @returns Try holding the result of f
 */
template <typename F>
typename std::enable_if<
  !std::is_same<typename std::result_of<F()>::type, void>::value,
  Try<typename std::result_of<F()>::type>>::type
makeTryWith(F&& f);

/*
 * Specialization of makeTryWith for void return
 *
 * @param f a function to execute and capture the result of
 *
 * @returns Try<void> holding the result of f
 */
template <typename F>
typename std::enable_if<
  std::is_same<typename std::result_of<F()>::type, void>::value,
  Try<void>>::type
makeTryWith(F&& f);

/**
 * Tuple<Try<Type>...> -> std::tuple<Type...>
 *
 * Unwraps a tuple-like type containing a sequence of Try<Type> instances to
 * std::tuple<Type>
 */
template <typename Tuple>
auto unwrapTryTuple(Tuple&&);

} // namespace folly

#include <folly/Try-inl.h>
