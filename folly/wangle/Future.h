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

#include <algorithm>
#include <exception>
#include <functional>
#include <memory>
#include <type_traits>
#include <vector>

#include "folly/MoveWrapper.h"
#include "Promise.h"
#include "Try.h"

namespace folly { namespace wangle {

template <typename T> struct isFuture;
template <class> class Later;

template <class T>
class Future {
 public:
  typedef T value_type;

  // not copyable
  Future(Future const&) = delete;
  Future& operator=(Future const&) = delete;

  // movable
  Future(Future&&) noexcept;
  Future& operator=(Future&&);

  ~Future();

  /** Return the reference to result. Should not be called if !isReady().
    Will rethrow the exception if an exception has been
    captured.

    This function is not thread safe - the returned Future can only
    be executed from the thread that the executor runs it in.
    See below for a thread safe version
    */
  typename std::add_lvalue_reference<T>::type
  value();
  typename std::add_lvalue_reference<const T>::type
  value() const;

  /// Returns a Later which will call back on the other side of executor.
  ///
  ///   f.via(e).then(a).then(b).launch();
  ///
  /// a and b will execute in the same context (the far side of e)
  template <typename Executor>
  Later<T> via(Executor* executor);

  /** True when the result (or exception) is ready. */
  bool isReady() const;

  /** A reference to the Try of the value */
  Try<T>& getTry();

  /** When this Future has completed, execute func which is a function that
    takes a Try<T>&&. A Future for the return type of func is
    returned. e.g.

    Future<string> f2 = f1.then([](Try<T>&&) { return string("foo"); });

    The Future given to the functor is ready, and the functor may call
    value(), which may rethrow if this has captured an exception. If func
    throws, the exception will be captured in the Future that is returned.
    */
  /* TODO n3428 and other async frameworks have something like then(scheduler,
     Future), we probably want to support a similar API (instead of
     via. or rather, via should return a cold future (Later) and we provide
     then(scheduler, Future) ). */
  template <class F>
  typename std::enable_if<
    !isFuture<typename std::result_of<F(Try<T>&&)>::type>::value,
    Future<typename std::result_of<F(Try<T>&&)>::type> >::type
  then(F&& func);

  /// Variant where func returns a Future<T> instead of a T. e.g.
  ///
  ///   Future<string> f2 = f1.then(
  ///     [](Try<T>&&) { return makeFuture<string>("foo"); });
  template <class F>
  typename std::enable_if<
    isFuture<typename std::result_of<F(Try<T>&&)>::type>::value,
    Future<typename std::result_of<F(Try<T>&&)>::type::value_type> >::type
  then(F&& func);

  /// Variant where func is an ordinary function (static method, method)
  ///
  ///   R doWork(Try<T>&&);
  ///
  ///   Future<R> f2 = f1.then(doWork);
  ///
  /// or
  ///
  ///   struct Worker {
  ///     static R doWork(Try<T>&&); }
  ///
  ///   Future<R> f2 = f1.then(&Worker::doWork);
  template <class = T, class R = std::nullptr_t>
  typename std::enable_if<!isFuture<R>::value, Future<R>>::type
  inline then(R(*func)(Try<T>&&)) {
    return then([func](Try<T>&& t) {
      return (*func)(std::move(t));
    });
  }

  /// Variant where func returns a Future<R> instead of a R. e.g.
  ///
  ///   struct Worker {
  ///     Future<R> doWork(Try<T>&&); }
  ///
  ///   Future<R> f2 = f1.then(&Worker::doWork);
  template <class = T, class R = std::nullptr_t>
  typename std::enable_if<isFuture<R>::value, R>::type
  inline then(R(*func)(Try<T>&&)) {
    return then([func](Try<T>&& t) {
      return (*func)(std::move(t));
    });
  }

  /// Variant where func is an member function
  ///
  ///   struct Worker {
  ///     R doWork(Try<T>&&); }
  ///
  ///   Worker *w;
  ///   Future<R> f2 = f1.then(w, &Worker::doWork);
  template <class = T, class R = std::nullptr_t, class Caller = std::nullptr_t>
  typename std::enable_if<!isFuture<R>::value, Future<R>>::type
  inline then(Caller *instance, R(Caller::*func)(Try<T>&&)) {
    return then([instance, func](Try<T>&& t) {
      return (instance->*func)(std::move(t));
    });
  }

  /// Variant where func returns a Future<R> instead of a R. e.g.
  ///
  ///   struct Worker {
  ///     Future<R> doWork(Try<T>&&); }
  ///
  ///   Worker *w;
  ///   Future<R> f2 = f1.then(w, &Worker::doWork);
  template <class = T, class R = std::nullptr_t, class Caller = std::nullptr_t>
  typename std::enable_if<isFuture<R>::value, R>::type
  inline then(Caller *instance, R(Caller::*func)(Try<T>&&)) {
    return then([instance, func](Try<T>&& t) {
      return (instance->*func)(std::move(t));
    });
  }

  /// Convenience method for ignoring the value and creating a Future<void>.
  /// Exceptions still propagate.
  Future<void> then();

  /// This is not the method you're looking for.
  ///
  /// This needs to be public because it's used by make* and when*, and it's
  /// not worth listing all those and their fancy template signatures as
  /// friends. But it's not for public consumption.
  template <class F>
  void setCallback_(F&& func);

 private:
  typedef detail::FutureObject<T>* objPtr;

  // shared state object
  objPtr obj_;

  explicit
  Future(objPtr obj) : obj_(obj) {}

  void throwIfInvalid() const;

  friend class Promise<T>;
};

/**
  Make a completed Future by moving in a value. e.g.

    string foo = "foo";
    auto f = makeFuture(std::move(foo));

  or

    auto f = makeFuture<string>("foo");
*/
template <class T>
Future<typename std::decay<T>::type> makeFuture(T&& t);

/** Make a completed void Future. */
Future<void> makeFuture();

/** Make a completed Future by executing a function. If the function throws
  we capture the exception, otherwise we capture the result. */
template <class F>
auto makeFutureTry(
  F&& func,
  typename std::enable_if<
    !std::is_reference<F>::value, bool>::type sdf = false)
  -> Future<decltype(func())>;

template <class F>
auto makeFutureTry(
  F const& func)
  -> Future<decltype(func())>;

/// Make a failed Future from an exception_ptr.
/// Because the Future's type cannot be inferred you have to specify it, e.g.
///
///   auto f = makeFuture<string>(std::current_exception());
template <class T>
Future<T> makeFuture(std::exception_ptr const& e);

/** Make a Future from an exception type E that can be passed to
  std::make_exception_ptr(). */
template <class T, class E>
typename std::enable_if<std::is_base_of<std::exception, E>::value, Future<T>>::type
makeFuture(E const& e);

/** Make a Future out of a Try */
template <class T>
Future<T> makeFuture(Try<T>&& t);

/** When all the input Futures complete, the returned Future will complete.
  Errors do not cause early termination; this Future will always succeed
  after all its Futures have finished (whether successfully or with an
  error).

  The Futures are moved in, so your copies are invalid. If you need to
  chain further from these Futures, use the variant with an output iterator.

  XXX is this still true?
  This function is thread-safe for Futures running on different threads.

  The return type for Future<T> input is a Future<std::vector<Try<T>>>
  */
template <class InputIterator>
Future<std::vector<Try<
  typename std::iterator_traits<InputIterator>::value_type::value_type>>>
whenAll(InputIterator first, InputIterator last);

/// This version takes a varying number of Futures instead of an iterator.
/// The return type for (Future<T1>, Future<T2>, ...) input
/// is a Future<std::tuple<Try<T1>, Try<T2>, ...>>.
/// The Futures are moved in, so your copies are invalid.
template <typename... Fs>
typename detail::VariadicContext<
  typename std::decay<Fs>::type::value_type...>::type
whenAll(Fs&&... fs);

/** The result is a pair of the index of the first Future to complete and
  the Try. If multiple Futures complete at the same time (or are already
  complete when passed in), the "winner" is chosen non-deterministically.

  This function is thread-safe for Futures running on different threads.
  */
template <class InputIterator>
Future<std::pair<
  size_t,
  Try<typename std::iterator_traits<InputIterator>::value_type::value_type>>>
whenAny(InputIterator first, InputIterator last);

/** when n Futures have completed, the Future completes with a vector of
  the index and Try of those n Futures (the indices refer to the original
  order, but the result vector will be in an arbitrary order)

  Not thread safe.
  */
template <class InputIterator>
Future<std::vector<std::pair<
  size_t,
  Try<typename std::iterator_traits<InputIterator>::value_type::value_type>>>>
whenN(InputIterator first, InputIterator last, size_t n);

/** Wait for the given future to complete on a semaphore. Returns a completed
 * future containing the result.
 *
 * NB if the promise for the future would be fulfilled in the same thread that
 * you call this, it will deadlock.
 */
template <class T>
Future<T> waitWithSemaphore(Future<T>&& f);

/** Wait for up to `timeout` for the given future to complete. Returns a future
 * which may or may not be completed depending whether the given future
 * completed in time
 *
 * Note: each call to this starts a (short-lived) thread and allocates memory.
 */
template <typename T, class Duration>
Future<T> waitWithSemaphore(Future<T>&& f, Duration timeout);

}} // folly::wangle

#include "Future-inl.h"
#include "Later.h"
