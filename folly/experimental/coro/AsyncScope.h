/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//
// Docs: https://fburl.com/fbcref_asyncscope
//

#pragma once

#include <folly/CancellationToken.h>
#include <folly/ExceptionWrapper.h>
#include <folly/experimental/coro/Coroutine.h>
#include <folly/experimental/coro/CurrentExecutor.h>
#include <folly/experimental/coro/Task.h>
#include <folly/experimental/coro/detail/Barrier.h>
#include <folly/experimental/coro/detail/BarrierTask.h>
#include <folly/futures/Future.h>
#include <folly/portability/SourceLocation.h>

#include <glog/logging.h>

#include <atomic>
#include <cassert>
#include <optional>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {

/**
 * The AsyncScope class is used to allow you to start a dynamic, unbounded
 * number of tasks, which can all run concurrently, and then later wait for
 * completion of all of the tasks.
 *
 * Tasks added to an AsyncScope must have a void or folly::Unit result-type
 * and must handle any errors prior to completing.
 *
 * @refcode folly/docs/examples/folly/experimental/coro/AsyncScope.cpp
 * @class folly::coro::AsyncScope
 */
//
// Example:
//    folly::coro::Task<void> process(Event event) {
//      try {
//        co_await do_processing(event.data);
//      } catch (...) {
//        LOG(ERROR) << "Processing event failed";
//      }
//    }
//
//    folly::coro::AsyncScope scope;
//    scope.add(processEvent(ev1));
//    scope.add(processEvent(ev2));
//    scope.add(processEvent(ev3));
//    co_await scope.joinAsync();
//
class AsyncScope {
 public:
  AsyncScope() noexcept;

  // @param throwOnJoin If true, will throw last unhandled exception (if any)
  //                    on joinAsync. Default behavior is to ignore the
  //                    exception in opt builds and FATAL in dev builds
  explicit AsyncScope(bool throwOnJoin) noexcept;

  // Destroy the AsyncScope object.
  //
  // NOTE: If you have called add() on this scope then you _must_
  // call either cleanup() or joinAsync() and wait until the that operation
  // completes before calling the destructor.
  ~AsyncScope();

  /**
   * Query the number of tasks added to the scope that have not yet completed.
   */
  std::size_t remaining() const noexcept;

  /**
   * Start the specified task/awaitable by co_awaiting it.
   *
   * Exceptions
   * ----------
   * IMPORTANT: Tasks submitted to the AsyncScope by calling .add() must
   * ensure they do not complete with an exception. Exceptions propagating
   * from the 'co_await awaitable' expression are logged using DFATAL.
   *
   * To avoid this occurring you should make sure to catch and handle any
   * exceptions within the task being started here.
   *
   * Interaction with cleanup/joinAsync
   * ----------------------------------
   * It is invalid to call add() once the joinAsync() or cleanup()
   * operations have completed.
   *
   * This generally means that it is unsafe to call .add() once cleanup()
   * has started as it may be racing with completion of cleanup().
   *
   * The exception to this rule is for cases where you know you are running
   * within a task that has been started with .add() and thus you know that
   * cleanup() will not yet have completed.
   *
   * Passing folly::coro::Task
   * -------------------------
   * NOTE: You cannot pass a folly::coro::Task to this method.
   * You must first call .scheduleOn() to specify which executor the task
   * should run on.
   */
  // returnAddress customize entry point to async stack (useful if this is
  // called from async code already). If not set will default to
  // FOLLY_ASYNC_STACK_RETURN_ADDRESS()
  template <typename Awaitable>
  void add(Awaitable&& awaitable, void* returnAddress = nullptr);

  template <typename Awaitable>
  void addWithSourceLoc(
      Awaitable&& awaitable,
      void* returnAddress = nullptr,
      source_location sourceLocation = source_location::current());

  /**
   * Asynchronously wait for all started tasks to complete.
   *
   * Either call this method _or_ cleanup() to join the work.
   * It is invalid to call both of them.
   */
  Task<void> joinAsync() noexcept;

  /**
   * Asynchronously cleanup all started tasks.
   *
   * If you have previuosly called add() then you must call cleanup()
   * and wait for the retuned future to complete before the AsyncScope
   * object destructs.
   */
  SemiFuture<Unit> cleanup() noexcept;

 private:
  template <typename Awaitable>
  static detail::DetachedBarrierTask addImpl(
      Awaitable awaitable,
      bool throwOnJoin,
      folly::exception_wrapper& maybeException,
      std::optional<source_location> source,
      std::atomic<bool>& exceptionRaised) {
    static_assert(
        std::is_void_v<await_result_t<Awaitable>> ||
            std::is_same_v<await_result_t<Awaitable>, folly::Unit>,
        "Result of the task would be discarded. Make sure task result is either void or folly::Unit.");

    try {
      co_await std::move(awaitable);
    } catch (const OperationCancelled&) {
    } catch (...) {
      if (throwOnJoin) {
        LOG(ERROR)
            << (source.has_value() ? sourceLocationToString(source.value())
                                   : "")
            << "Unhandled exception thrown from task added to AsyncScope: "
            << folly::exceptionStr(std::current_exception());
        if (!exceptionRaised.exchange(true)) {
          maybeException = folly::exception_wrapper(std::current_exception());
        }
      } else {
        LOG(DFATAL)
            << (source.has_value() ? sourceLocationToString(source.value())
                                   : "")
            << "Unhandled exception thrown from task added to AsyncScope: "
            << folly::exceptionStr(std::current_exception());
      }
    }
  }

  detail::Barrier barrier_{1};
  std::atomic<bool> anyTasksStarted_{false};
  bool joinStarted_{false};
  bool joined_{false};
  bool throwOnJoin_{false};
  folly::exception_wrapper maybeException_;
  std::atomic<bool> exceptionRaised_{false};
};

inline AsyncScope::AsyncScope() noexcept : throwOnJoin_(false) {}

inline AsyncScope::AsyncScope(bool throwOnJoin) noexcept
    : throwOnJoin_(throwOnJoin) {}

inline AsyncScope::~AsyncScope() {
  CHECK(!anyTasksStarted_.load(std::memory_order_relaxed) || joined_)
      << "AsyncScope::cleanup() not yet complete";
}

inline std::size_t AsyncScope::remaining() const noexcept {
  const std::size_t count = barrier_.remaining();
  return joinStarted_ ? count : (count > 1 ? count - 1 : 0);
}

template <typename Awaitable>
FOLLY_NOINLINE inline void AsyncScope::add(
    Awaitable&& awaitable, void* returnAddress) {
  assert(
      !joined_ &&
      "It is invalid to add() more work after work has been joined");
  anyTasksStarted_.store(true, std::memory_order_relaxed);
  addImpl(
      static_cast<Awaitable&&>(awaitable),
      throwOnJoin_,
      maybeException_,
      std::nullopt,
      exceptionRaised_)
      .start(
          &barrier_,
          returnAddress ? returnAddress : FOLLY_ASYNC_STACK_RETURN_ADDRESS());
}

template <typename Awaitable>
FOLLY_NOINLINE inline void AsyncScope::addWithSourceLoc(
    Awaitable&& awaitable,
    void* returnAddress,
    source_location sourceLocation) {
  assert(
      !joined_ &&
      "It is invalid to add() more work after work has been joined");
  anyTasksStarted_.store(true, std::memory_order_relaxed);
  addImpl(
      static_cast<Awaitable&&>(awaitable),
      throwOnJoin_,
      maybeException_,
      sourceLocation,
      exceptionRaised_)
      .start(
          &barrier_,
          returnAddress ? returnAddress : FOLLY_ASYNC_STACK_RETURN_ADDRESS());
}

inline Task<void> AsyncScope::joinAsync() noexcept {
  assert(!joinStarted_ && "It is invalid to join a scope multiple times");
  joinStarted_ = true;
  co_await barrier_.arriveAndWait();
  joined_ = true;
  if (maybeException_.has_exception_ptr()) {
    maybeException_.throw_exception();
  }
}

inline folly::SemiFuture<folly::Unit> AsyncScope::cleanup() noexcept {
  return joinAsync().semi();
}

/**
 * A cancellable version of AsyncScope. Work added to this scope will be
 * provided a cancellation token for cancelling during join.
 *
 * See add() and cancelAndJoinAsync() for more information.
 *
 * Note: Task and AsyncGenerator will ignore the internal cancellation
 * signal if they already have a cancellation token (i.e. if someone has already
 * called co_withCancellation on them.)
 * If you need an external cancellation signal as well, pass that token to this
 * constructor or to add() instead of attaching it to the Awaitable.
 *
 * @refcode
 * folly/docs/examples/folly/experimental/coro/CancellableAsyncScope.cpp
 * @class folly::coro::CancellableAsyncScope
 */
class CancellableAsyncScope {
 public:
  CancellableAsyncScope() noexcept
      : cancellationToken_(cancellationSource_.getToken()) {}
  explicit CancellableAsyncScope(bool throwOnJoin) noexcept
      : cancellationToken_(cancellationSource_.getToken()),
        scope_(throwOnJoin) {}
  explicit CancellableAsyncScope(CancellationToken&& token)
      : cancellationToken_(CancellationToken::merge(
            cancellationSource_.getToken(), std::move(token))) {}
  CancellableAsyncScope(CancellationToken&& token, bool throwOnJoin)
      : cancellationToken_(CancellationToken::merge(
            cancellationSource_.getToken(), std::move(token))),
        scope_(throwOnJoin) {}

  /**
   * Query the number of tasks added to the scope that have not yet completed.
   */
  std::size_t remaining() const noexcept { return scope_.remaining(); }

  /**
   * Start the specified task/awaitable by co_awaiting it. The awaitable will be
   * provided a cancellation token to respond to cancelAndJoinAsync() in the
   * future.
   *
   * An additional cancellation token may be passed in to apply to the
   * awaitable; it will be merged with the internal token.
   *
   * Note that cancellation is cooperative, your task must handle cancellation
   * in order to have any effect.
   *
   * See the documentation on AsyncScope::add.
   */
  template <typename Awaitable>
  void add(
      Awaitable&& awaitable,
      std::optional<CancellationToken> token = std::nullopt,
      void* returnAddress = nullptr) {
    scope_.add(
        co_withCancellation(
            token ? CancellationToken::merge(*token, cancellationToken_)
                  : cancellationToken_,
            static_cast<Awaitable&&>(awaitable)),
        returnAddress ? returnAddress : FOLLY_ASYNC_STACK_RETURN_ADDRESS());
  }

  template <typename Awaitable>
  void addWithSourceLoc(
      Awaitable&& awaitable,
      std::optional<CancellationToken> token,
      void* returnAddress = nullptr,
      source_location sourceLocation = source_location::current()) {
    scope_.addWithSourceLoc(
        co_withCancellation(
            token ? CancellationToken::merge(*token, cancellationToken_)
                  : cancellationToken_,
            static_cast<Awaitable&&>(awaitable)),
        returnAddress ? returnAddress : FOLLY_ASYNC_STACK_RETURN_ADDRESS(),
        sourceLocation);
  }

  template <typename Awaitable>
  void addWithSourceLoc(
      Awaitable&& awaitable,
      source_location sourceLocation = source_location::current()) {
    addWithSourceLoc(
        std::forward<Awaitable>(awaitable),
        std::nullopt,
        nullptr,
        std::move(sourceLocation));
  }

  /**
   * Schedules the given task on the current executor and adds it to the
   * AsyncScope. The task will be provided a cancellation token to respond to
   * cancelAndJoinAsync() in the future.
   *
   * Note that cancellation is cooperative, your task must handle cancellation
   * in order to have any effect.
   */
  template <class T>
  folly::coro::Task<void> co_schedule(folly::coro::Task<T>&& task) {
    add(std::move(task).scheduleOn(co_await co_current_executor));
    co_return;
  }

  /**
   * Request cancellation for all started tasks that accepted a
   * CancellationToken in add().
   */
  void requestCancellation() const noexcept {
    cancellationSource_.requestCancellation();
  }

  /**
   * Query if cancellation was requested on the tasks added to this AsyncScope.
   *
   * This will return true if either cancellation was requested using
   * `requestCancellation()` method of this scope OR if cancellation is
   * requested on the token passed into the constructor.
   */
  bool isScopeCancellationRequested() const noexcept {
    return cancellationToken_.isCancellationRequested();
  }

  /**
   * Request cancellation and asynchronously wait for all started tasks to
   * complete.
   *
   * Either call this method, _or_ joinAsync() to join the work. It is invalid
   * to call both of them.
   */
  Task<void> cancelAndJoinAsync() noexcept {
    requestCancellation();
    co_await joinAsync();
  }

  /**
   * Asynchronously wait for all started tasks to complete without requesting
   * cancellation.
   *
   * Either call this method _or_ cancelAndJoinAsync() to join the
   * work. It is invalid to call both of them.
   */
  Task<void> joinAsync() noexcept { co_await scope_.joinAsync(); }

 private:
  folly::CancellationSource cancellationSource_;
  CancellationToken cancellationToken_;
  AsyncScope scope_;
};

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
