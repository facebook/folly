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

#include <folly/wangle/Executor.h>
#include <folly/wangle/Future.h>
#include <folly/Optional.h>

namespace folly { namespace wangle {

template <typename T> struct isLaterOrFuture;
template <typename T> struct isLater;

/*
 * Later is like a cold Future, but makes it easier to avoid triggering until
 * later, because it must be triggered explicitly. An equivalence example will
 * help differentiate:
 *
 *   Later<Foo> later =
 *     Later<Foo>(std::move(foo))
 *     .then(cb1)
 *     .via(ex1)
 *     .then(cb2)
 *     .then(cb3)
 *     .via(ex2)
 *     .then(cb4)
 *     .then(cb5);
 *   ...
 *   later.launch();
 *
 *   Future<Foo> coldFuture = makeFuture(std::move(foo));
 *   coldFuture.deactivate();
 *   coldFuture
 *     .then(cb1)
 *     .via(ex1)
 *     .then(cb2)
 *     .then(cb3)
 *     .via(ex2)
 *     .then(cb4)
 *     .then(cb5);
 *   ...
 *   coldFuture.activate();
 *
 * Using a Later means you don't have to grab a handle to the first Future and
 * deactivate it.
 *
 * Later used to be a workaround to the thread-unsafe nature of Future
 * chaining, but that has changed and there is no need to use Later if your
 * only goal is to traverse thread boundaries with executors. In that case,
 * just use Future::via().
 *
 * Here is an example of a workflow:
 *
 * Later<ClientRequest> later(std::move(request));
 *
 * auto future = later.
 *   .via(cpuExecutor)
 *   .then([=](Try<ClientRequest>&& t) { return doCpuWork(t.value()); })
 *   .via(diskExecutor)
 *   .then([=](Try<CpuResponse>&& t) { return doDiskWork(t.value()); })
 *   .via(serverExecutor)
 *   .then([=]Try<DiskResponse>&& t) { return sendClientResponse(t.value()); })
 *   .launch();
 */
template <class T>
class Later {
 public:
  typedef T value_type;

  /*
   * This default constructor is used to build an asynchronous workflow that
   * takes no input.
   */
  template <class U = void,
            class = typename std::enable_if<std::is_void<U>::value>::type,
            class = typename std::enable_if<std::is_same<T, U>::value>::type>
  Later();

  /*
   * Lift a Future into a Later
   */
  /* implicit */ Later(Future<T>&& f);

  /*
   * This constructor is used to build an asynchronous workflow that takes a
   * value as input, and that value is passed in.
   */
  template <class U,
            class = typename std::enable_if<!std::is_void<U>::value>::type,
            class = typename std::enable_if<std::is_same<T, U>::value>::type>
  explicit Later(U&& input);

  /*
   * This constructor is used to wrap a pre-existing cob-style asynchronous api
   * so that it can be used in wangle. wangle provides the callback to this
   * pre-existing api, and this callback will fulfill a promise so as to
   * incorporate this api into the workflow.
   *
   * Example usage:
   *
   * // This adds two ints asynchronously. cob is called in another thread.
   * void addAsync(int a, int b, std::function<void(int&&)>&& cob);
   *
   * Later<int> asyncWrapper([=](std::function<void(int&&)>&& fn) {
   *   addAsync(1, 2, std::move(fn));
   * });
   */
  // TODO we should implement a makeFuture-ish with this pattern too, now.
  template <class U,
            class = typename std::enable_if<!std::is_void<U>::value>::type,
            class = typename std::enable_if<std::is_same<T, U>::value>::type>
  explicit Later(std::function<void(std::function<void(U&&)>&&)>&& fn);

  /*
   * then() adds additional work to the end of the workflow. If the lambda
   * provided to then() returns a future, that future must be fulfilled in the
   * same thread of the last set executor (either at constructor or from a call
   * to via()).
   */
  template <class F>
  typename std::enable_if<
    !isLaterOrFuture<typename std::result_of<F(Try<T>&&)>::type>::value,
    Later<typename std::result_of<F(Try<T>&&)>::type> >::type
  then(F&& fn);

  template <class F>
  typename std::enable_if<
    isFuture<typename std::result_of<F(Try<T>&&)>::type>::value,
    Later<typename std::result_of<F(Try<T>&&)>::type::value_type> >::type
  then(F&& fn);

  /*
   * If the function passed to then() returns a Later<T>, calls to then() will
   * be chained to the new Later before launching the new Later.
   *
   * This can be used to build asynchronous modules that can be called from a
   * user thread and completed in a callback thread.
   *
   * Using the Later(std::function<void(std::function<void(T&&)>)>&& fn)
   * constructor, you can wrap existing asynchronous modules with a Later and
   * can chain it to wangle asynchronous workflows via this call.
   */
  template <class F>
  typename std::enable_if<
    isLater<typename std::result_of<F(Try<T>&&)>::type>::value,
    Later<typename std::result_of<F(Try<T>&&)>::type::value_type> >::type
  then(F&& fn);

  /*
   * Resets the executor - all then() calls made after the call to via() will be
   * made in the new executor. The Executor must outlive.
   */
  Later<T> via(Executor* executor);

  /*
   * Starts the workflow. The function provided in the constructor will be
   * called in the executor provided in the constructor. Subsequent then()
   * calls will be made, potentially changing threads if a via() call is made.
   * The future returned will be fulfilled in the last executor.
   */
  Future<T> launch();

  /*
   * Deprecated. Use launch()
   */
  void fireAndForget() __attribute__ ((deprecated)) { launch(); }

 private:
  Promise<void> starter_;
  folly::Optional<Future<T>> future_;

  struct hide { };

  explicit Later(Promise<void>&& starter);

  template <class U>
  friend class Later;
};

}}

#include "Later-inl.h"
