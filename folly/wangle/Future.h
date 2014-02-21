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

#include "folly/MoveWrapper.h"
#include "Promise.h"
#include "Try.h"

namespace folly { namespace wangle {

template <typename T> struct isFuture;

template <class T>
class Future {
 public:
  typedef T value_type;

  // not copyable
  Future(Future const&) = delete;
  Future& operator=(Future const&) = delete;

  // movable
  Future(Future&&);
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

  template <typename Executor>
  Future<T> executeWithSameThread(Executor* executor);

  /**
     Thread-safe version of executeWith

     Since an executor would likely start executing the Future chain
     right away, it would be a race condition to call:
     Future.executeWith(...).then(...), as there would be race
     condition between the then and the running Future.
     Instead, you may pass in a Promise so that we can set up
     the rest of the chain in advance, without any racey
     modifications of the continuation
   */
  template <typename Executor>
  void executeWith(Executor* executor, Promise<T>&& cont_promise);

  /** True when the result (or exception) is ready.  value() will not block
      when this returns true. */
  bool isReady() const;

  /** A reference to the Try of the value */
  Try<T>& getTry();

  /** When this Future has completed, execute func which is a function that
    takes a Try<T>&&. A Future for the return type of func is
    returned. e.g.

    Future<string> f2 = f1.then([](Try<T>&&) { return string("foo"); });

    The functor given may call value() without blocking, which may rethrow if
    this has captured an exception. If func throws, the exception will be
    captured in the Future that is returned.
    */
  /* n3428 has then(scheduler&, F&&), we might want to reorganize to use
     similar API. or maybe not */
  template <class F>
  typename std::enable_if<
    !isFuture<typename std::result_of<F(Try<T>&&)>::type>::value,
    Future<typename std::result_of<F(Try<T>&&)>::type> >::type
  then(F&& func);

  template <class F>
  typename std::enable_if<
    isFuture<typename std::result_of<F(Try<T>&&)>::type>::value,
    Future<typename std::result_of<F(Try<T>&&)>::type::value_type> >::type
  then(F&& func);

  /** Use this method on the Future when we don't really care about the
    returned value and want to convert the Future<T> to a Future<void>
    Convenience function
    */
  Future<void> then();

  template <class F>
  void setContinuation(F&& func);

 private:
  /* Eventually this may not be a shared_ptr, but something similar without
     expensive thread-safety. */
  typedef detail::FutureObject<T>* objPtr;

  // shared state object
  objPtr obj_;

  explicit
  Future(objPtr obj) : obj_(obj) {}

  void throwIfInvalid() const;

  friend class Promise<T>;
};

/** Make a completed Future by moving in a value. e.g.
  auto f = makeFuture(string("foo"));
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

/** Make a completed (error) Future from an exception_ptr. Because the type
can't be inferred you have to give it, e.g.

auto f = makeFuture<string>(std::current_exception());
*/
template <class T>
Future<T> makeFuture(std::exception_ptr const& e);

/** Make a Future from an exception type E that can be passed to
  std::make_exception_ptr(). */
template <class T, class E>
typename std::enable_if<std::is_base_of<std::exception, E>::value, Future<T>>::type
makeFuture(E const& e);

/** When all the input Futures complete, the returned Future will complete.
  Errors do not cause early termination; this Future will always succeed
  after all its Futures have finished (whether successfully or with an
  error).

  The Futures are moved in, so your copies are invalid. If you need to
  chain further from these Futures, use the variant with an output iterator.

  This function is thread-safe for Futures running on different threads.

  The return type for Future<T> input is a Future<vector<Try<T>>>
  */
template <class InputIterator>
Future<std::vector<Try<
  typename std::iterator_traits<InputIterator>::value_type::value_type>>>
whenAll(InputIterator first, InputIterator last);

/** This version takes a varying number of Futures instead of an iterator.
  The return type for (Future<T1>, Future<T2>, ...) input
  is a Future<tuple<Try<T1>, Try<T2>, ...>>.
  */
template <typename... Fs>
typename detail::VariadicContext<typename Fs::value_type...>::type
whenAll(Fs&... fs);

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

}} // folly::wangle

#include "Future-inl.h"

/*

TODO

I haven't included a Future<T&> specialization because I don't forsee us
using it, however it is not difficult to add when needed. Refer to
Future<void> for guidance. std::Future and boost::Future code would also be
instructive.

I think that this might be a good candidate for folly, once it has baked for
awhile.

*/
