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

#include "folly/wangle/Executor.h"
#include "folly/wangle/Future.h"
#include "folly/Optional.h"

namespace folly { namespace wangle {

template <typename T> struct isLaterOrFuture;
template <typename T> struct isLater;

/*
 * Since wangle primitives (promise/future) are not thread safe, it is difficult
 * to build complex asynchronous workflows. A Later allows you to build such a
 * workflow before actually launching it so that callbacks can be set in a
 * threadsafe manner.
 *
 * The interface to add additional work is the same as future: a then() method
 * that takes a function that can return either a type T, a Future<T>, or a
 * Later<T>
 *
 * Thread transitions are done by using executors and calling the via() method.
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
 *
 * Although this workflow traverses many threads, we are able to string
 * continuations together in a threadsafe manner.
 *
 * Laters can also be used to wrap preexisting asynchronous modules that were
 * not built with wangle in mind. You can create a Later with a function that
 * takes a callback as input. The function will not actually be called until
 * launch(), allowing you to string then() statements on top of the callback.
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
   * so that it can be used in wangle in a threadsafe manner. wangle provides
   * the callback to this pre-existing api, and this callback will fulfill a
   * promise so as to incorporate this api into the workflow.
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
   * user thread and completed in a callback thread. Callbacks can be set up
   * ahead of time without thread safety issues.
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
   *
   * Thread safety issues of Futures still apply. If you want to wait on the
   * Future, it must be done in the thread that will fulfil it.
   */
  Future<T> launch();

  /*
   * Same as launch, only no Future is returned. This guarantees thread safe
   * cleanup of the internal Futures, even if the Later completes in a different
   * thread than the thread that calls fireAndForget().
   *
   * Deprecated. Use launch()
   */
  void fireAndForget() { launch(); }

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
