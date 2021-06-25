/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <utility>

#include <folly/CancellationToken.h>
#include <folly/ExceptionWrapper.h>
#include <folly/experimental/coro/AsyncPipe.h>
#include <folly/experimental/coro/AsyncScope.h>
#include <folly/experimental/coro/Mutex.h>
#include <folly/experimental/coro/detail/Barrier.h>
#include <folly/experimental/coro/detail/BarrierTask.h>
#include <folly/experimental/coro/detail/CurrentAsyncFrame.h>
#include <folly/experimental/coro/detail/Helpers.h>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {
namespace detail {

template <typename T>
T&& getValueOrUnit(Try<T>&& value) {
  assert(value.hasValue());
  return std::move(value).value();
}

inline Unit getValueOrUnit([[maybe_unused]] Try<void>&& value) {
  assert(value.hasValue());
  return Unit{};
}

template <typename SemiAwaitable, typename Result>
BarrierTask makeCollectAllTryTask(
    Executor::KeepAlive<> executor,
    const CancellationToken& cancelToken,
    SemiAwaitable&& awaitable,
    Try<Result>& result) {
  try {
    if constexpr (std::is_void_v<Result>) {
      co_await co_viaIfAsync(
          std::move(executor),
          co_withCancellation(
              cancelToken, static_cast<SemiAwaitable&&>(awaitable)));
      result.emplace();
    } else {
      result.emplace(co_await co_viaIfAsync(
          std::move(executor),
          co_withCancellation(
              cancelToken, static_cast<SemiAwaitable&&>(awaitable))));
    }
  } catch (...) {
    result.emplaceException(std::current_exception());
  }
}

template <typename... SemiAwaitables, size_t... Indices>
auto collectAllTryImpl(
    std::index_sequence<Indices...>, SemiAwaitables... awaitables)
    -> folly::coro::Task<
        std::tuple<collect_all_try_component_t<SemiAwaitables>...>> {
  static_assert(sizeof...(Indices) == sizeof...(SemiAwaitables));
  if constexpr (sizeof...(SemiAwaitables) == 0) {
    co_return std::tuple<>{};
  } else {
    const Executor::KeepAlive<> executor = co_await co_current_executor;
    const CancellationToken& cancelToken =
        co_await co_current_cancellation_token;

    std::tuple<collect_all_try_component_t<SemiAwaitables>...> results;

    folly::coro::detail::BarrierTask tasks[sizeof...(SemiAwaitables)] = {
        makeCollectAllTryTask(
            executor.get_alias(),
            cancelToken,
            static_cast<SemiAwaitables&&>(awaitables),
            std::get<Indices>(results))...,
    };

    folly::coro::detail::Barrier barrier{sizeof...(SemiAwaitables) + 1};

    auto& asyncFrame = co_await detail::co_current_async_stack_frame;

    // Use std::initializer_list to ensure that the sub-tasks are launched
    // in the order they appear in the parameter pack.

    // Save the initial context and restore it after starting each task
    // as the task may have modified the context before suspending and we
    // want to make sure the next task is started with the same initial
    // context.
    const auto context = RequestContext::saveContext();
    (void)std::initializer_list<int>{
        (tasks[Indices].start(&barrier, asyncFrame),
         RequestContext::setContext(context),
         0)...};

    // Wait for all of the sub-tasks to finish execution.
    // Should be safe to avoid an executor transition here even if the
    // operation completes asynchronously since all of the child tasks
    // should already have transitioned to the correct executor due to
    // the use of co_viaIfAsync() within makeCollectAllTryTask().
    co_await UnsafeResumeInlineSemiAwaitable{barrier.arriveAndWait()};

    co_return results;
  }
}

template <typename... SemiAwaitables, size_t... Indices>
auto collectAllImpl(
    std::index_sequence<Indices...>, SemiAwaitables... awaitables)
    -> folly::coro::Task<
        std::tuple<collect_all_component_t<SemiAwaitables>...>> {
  if constexpr (sizeof...(SemiAwaitables) == 0) {
    co_return std::tuple<>{};
  } else {
    const Executor::KeepAlive<> executor = co_await co_current_executor;
    const CancellationToken& parentCancelToken =
        co_await co_current_cancellation_token;

    const CancellationSource cancelSource;
    CancellationCallback cancelCallback(parentCancelToken, [&]() noexcept {
      cancelSource.requestCancellation();
    });
    const CancellationToken cancelToken = cancelSource.getToken();

    exception_wrapper firstException;
    std::atomic<bool> anyFailures{false};

    auto makeTask = [&](auto&& awaitable, auto& result) -> BarrierTask {
      using await_result = semi_await_result_t<decltype(awaitable)>;
      try {
        if constexpr (std::is_void_v<await_result>) {
          co_await co_viaIfAsync(
              executor.get_alias(),
              co_withCancellation(
                  cancelToken, static_cast<decltype(awaitable)>(awaitable)));
          result.emplace();
        } else {
          result.emplace(co_await co_viaIfAsync(
              executor.get_alias(),
              co_withCancellation(
                  cancelToken, static_cast<decltype(awaitable)>(awaitable))));
        }
      } catch (...) {
        anyFailures.store(true, std::memory_order_relaxed);
        if (!cancelSource.requestCancellation()) {
          // This was the first failure, remember it's error.
          firstException = exception_wrapper{std::current_exception()};
        }
      }
    };

    std::tuple<collect_all_try_component_t<SemiAwaitables>...> results;

    folly::coro::detail::BarrierTask tasks[sizeof...(SemiAwaitables)] = {
        makeTask(
            static_cast<SemiAwaitables&&>(awaitables),
            std::get<Indices>(results))...,
    };

    folly::coro::detail::Barrier barrier{sizeof...(SemiAwaitables) + 1};

    // Save the initial context and restore it after starting each task
    // as the task may have modified the context before suspending and we
    // want to make sure the next task is started with the same initial
    // context.
    const auto context = RequestContext::saveContext();

    auto& asyncFrame = co_await detail::co_current_async_stack_frame;

    // Use std::initializer_list to ensure that the sub-tasks are launched
    // in the order they appear in the parameter pack.
    (void)std::initializer_list<int>{
        (tasks[Indices].start(&barrier, asyncFrame),
         RequestContext::setContext(context),
         0)...};

    // Wait for all of the sub-tasks to finish execution.
    // Should be safe to avoid an executor transition here even if the
    // operation completes asynchronously since all of the child tasks
    // should already have transitioned to the correct executor due to
    // the use of co_viaIfAsync() within makeBarrierTask().
    co_await UnsafeResumeInlineSemiAwaitable{barrier.arriveAndWait()};

    if (anyFailures.load(std::memory_order_relaxed)) {
      if (firstException) {
        co_yield co_error(std::move(firstException));
      }

      // Parent task was cancelled before any child tasks failed.
      // Complete with the OperationCancelled error instead of the
      // child task's errors.
      co_yield co_cancelled;
    }

    co_return std::tuple<collect_all_component_t<SemiAwaitables>...>{
        getValueOrUnit(std::get<Indices>(std::move(results)))...};
  }
}

template <typename InputRange, typename IsTry>
auto makeUnorderedAsyncGeneratorFromAwaitableRangeImpl(
    AsyncScope& scope, InputRange awaitables, IsTry) {
  using Item =
      async_generator_from_awaitable_range_item_t<InputRange, IsTry::value>;
  return [](AsyncScope& scopeParam,
            InputRange awaitablesParam) -> AsyncGenerator<Item&&> {
    auto [results, pipe] = AsyncPipe<Item, false>::create();
    const CancellationSource cancelSource;
    auto guard = folly::makeGuard([&] { cancelSource.requestCancellation(); });
    auto ex = co_await co_current_executor;
    size_t expected = 0;
    // Save the initial context and restore it after starting each task
    // as the task may have modified the context before suspending and we
    // want to make sure the next task is started with the same initial
    // context.
    const auto context = RequestContext::saveContext();

    for (auto&& semiAwaitable : static_cast<InputRange&&>(awaitablesParam)) {
      scopeParam.add(
          [](auto semiAwaitableParam,
             auto& cancelSourceParam,
             auto& p) -> Task<void> {
            auto result = co_await co_withCancellation(
                cancelSourceParam.getToken(),
                co_awaitTry(std::move(semiAwaitableParam)));
            if (!result.hasValue() && !IsTry::value) {
              cancelSourceParam.requestCancellation();
            }
            p.write(std::move(result));
          }(static_cast<decltype(semiAwaitable)&&>(semiAwaitable),
            cancelSource,
            pipe)
                             .scheduleOn(ex));
      ++expected;
      RequestContext::setContext(context);
    }

    while (true) {
      CancellationCallback cancelCallback(
          co_await co_current_cancellation_token,
          [&]() noexcept { cancelSource.requestCancellation(); });

      if constexpr (!IsTry::value) {
        auto result = co_await co_awaitTry(results.next());
        if (result.hasValue()) {
          co_yield std::move(*result);
          if (--expected) {
            continue;
          }
          result = {}; // completion result
        }
        guard.dismiss();
        co_yield co_result(std::move(result));
      } else {
        // Prevent AsyncPipe from receiving cancellation so we get the right
        // number of OperationCancelleds.
        auto result = co_await co_withCancellation({}, results.next());
        co_yield std::move(*result);
        if (--expected == 0) {
          guard.dismiss();
          co_return;
        }
      }
    }
  }(scope, std::move(awaitables));
}

template <typename... SemiAwaitables, size_t... Indices>
auto collectAnyImpl(
    std::index_sequence<Indices...>, SemiAwaitables&&... awaitables)
    -> folly::coro::Task<std::pair<
        std::size_t,
        folly::Try<collect_any_component_t<SemiAwaitables...>>>> {
  const CancellationToken& parentCancelToken =
      co_await co_current_cancellation_token;
  const CancellationSource cancelSource;
  const CancellationToken cancelToken =
      CancellationToken::merge(parentCancelToken, cancelSource.getToken());

  std::pair<std::size_t, folly::Try<collect_any_component_t<SemiAwaitables...>>>
      firstCompletion;
  firstCompletion.first = size_t(-1);
  co_await folly::coro::collectAll(folly::coro::co_withCancellation(
      cancelToken,
      folly::coro::co_invoke(
          [&, aw = static_cast<SemiAwaitables&&>(awaitables)]() mutable
          -> folly::coro::Task<void> {
            auto result = co_await folly::coro::co_awaitTry(
                static_cast<SemiAwaitables&&>(aw));
            if (!cancelSource.requestCancellation()) {
              // This is first entity to request cancellation.
              firstCompletion.first = Indices;
              firstCompletion.second = std::move(result);
            }
          }))...);

  if (parentCancelToken.isCancellationRequested()) {
    co_yield co_cancelled;
  }

  co_return firstCompletion;
}

template <typename... SemiAwaitables, size_t... Indices>
auto collectAnyNoDiscardImpl(
    std::index_sequence<Indices...>, SemiAwaitables&&... awaitables)
    -> folly::coro::Task<
        std::tuple<collect_all_try_component_t<SemiAwaitables>...>> {
  const CancellationToken& parentCancelToken =
      co_await co_current_cancellation_token;
  const CancellationSource cancelSource;
  const CancellationToken cancelToken =
      CancellationToken::merge(parentCancelToken, cancelSource.getToken());

  std::tuple<collect_all_try_component_t<SemiAwaitables>...> results;
  co_await folly::coro::collectAll(folly::coro::co_withCancellation(
      cancelToken, folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
        auto result = co_await folly::coro::co_awaitTry(
            std::forward<SemiAwaitables>(awaitables));
        cancelSource.requestCancellation();
        std::get<Indices>(results) = std::move(result);
      }))...);

  co_return results;
}

} // namespace detail

template <typename... SemiAwaitables>
auto collectAll(SemiAwaitables&&... awaitables) -> folly::coro::Task<std::tuple<
    detail::collect_all_component_t<remove_cvref_t<SemiAwaitables>>...>> {
  return detail::collectAllImpl(
      std::make_index_sequence<sizeof...(SemiAwaitables)>{},
      static_cast<SemiAwaitables&&>(awaitables)...);
}

template <typename... SemiAwaitables>
auto collectAllTry(SemiAwaitables&&... awaitables)
    -> folly::coro::Task<std::tuple<detail::collect_all_try_component_t<
        remove_cvref_t<SemiAwaitables>>...>> {
  return detail::collectAllTryImpl(
      std::make_index_sequence<sizeof...(SemiAwaitables)>{},
      static_cast<SemiAwaitables&&>(awaitables)...);
}

template <
    typename InputRange,
    std::enable_if_t<
        !std::is_void_v<
            semi_await_result_t<detail::range_reference_t<InputRange>>>,
        int>>
auto collectAllRange(InputRange awaitables)
    -> folly::coro::Task<std::vector<detail::collect_all_range_component_t<
        detail::range_reference_t<InputRange>>>> {
  const folly::Executor::KeepAlive<> executor = co_await co_current_executor;

  const CancellationSource cancelSource;
  CancellationCallback cancelCallback(
      co_await co_current_cancellation_token,
      [&]() noexcept { cancelSource.requestCancellation(); });
  const CancellationToken cancelToken = cancelSource.getToken();

  std::vector<detail::collect_all_try_range_component_t<
      detail::range_reference_t<InputRange>>>
      tryResults;

  exception_wrapper firstException;
  std::atomic<bool> anyFailures = false;

  using awaitable_type = remove_cvref_t<detail::range_reference_t<InputRange>>;
  auto makeTask = [&](awaitable_type semiAwaitable,
                      std::size_t index) -> detail::BarrierTask {
    assert(index < tryResults.size());

    try {
      tryResults[index].emplace(co_await co_viaIfAsync(
          executor.get_alias(),
          co_withCancellation(cancelToken, std::move(semiAwaitable))));
    } catch (...) {
      anyFailures.store(true, std::memory_order_relaxed);
      if (!cancelSource.requestCancellation()) {
        firstException = exception_wrapper{std::current_exception()};
      }
    }
  };

  // Create a task to await each input awaitable.
  std::vector<detail::BarrierTask> tasks;

  // TODO: Detect when the input range supports constant-time
  // .size() and pre-reserve storage for that many elements in 'tasks'.

  std::size_t taskCount = 0;
  for (auto&& semiAwaitable : static_cast<InputRange&&>(awaitables)) {
    tasks.push_back(makeTask(
        static_cast<decltype(semiAwaitable)&&>(semiAwaitable), taskCount++));
  }

  tryResults.resize(taskCount);

  // Save the initial context and restore it after starting each task
  // as the task may have modified the context before suspending and we
  // want to make sure the next task is started with the same initial
  // context.
  const auto context = RequestContext::saveContext();

  auto& asyncFrame = co_await detail::co_current_async_stack_frame;

  // Launch the tasks and wait for them all to finish.
  {
    detail::Barrier barrier{tasks.size() + 1};
    for (auto&& task : tasks) {
      task.start(&barrier, asyncFrame);
      RequestContext::setContext(context);
    }
    co_await detail::UnsafeResumeInlineSemiAwaitable{barrier.arriveAndWait()};
  }

  // Check if there were any exceptions and rethrow the first one.
  if (anyFailures.load(std::memory_order_relaxed)) {
    if (firstException) {
      co_yield co_error(std::move(firstException));
    }

    // Cancellation was requested of the parent Task before any of the
    // child tasks failed.
    co_yield co_cancelled;
  }

  std::vector<detail::collect_all_range_component_t<
      detail::range_reference_t<InputRange>>>
      results;
  results.reserve(tryResults.size());
  for (auto& result : tryResults) {
    results.emplace_back(std::move(result).value());
  }

  co_return results;
}

template <
    typename InputRange,
    std::enable_if_t<
        std::is_void_v<
            semi_await_result_t<detail::range_reference_t<InputRange>>>,
        int>>
auto collectAllRange(InputRange awaitables) -> folly::coro::Task<void> {
  const folly::Executor::KeepAlive<> executor = co_await co_current_executor;

  CancellationSource cancelSource;
  CancellationCallback cancelCallback(
      co_await co_current_cancellation_token,
      [&]() noexcept { cancelSource.requestCancellation(); });
  const CancellationToken cancelToken = cancelSource.getToken();

  exception_wrapper firstException;
  std::atomic<bool> anyFailures = false;

  using awaitable_type = remove_cvref_t<detail::range_reference_t<InputRange>>;
  auto makeTask = [&](awaitable_type semiAwaitable) -> detail::BarrierTask {
    try {
      co_await co_viaIfAsync(
          executor.get_alias(),
          co_withCancellation(cancelToken, std::move(semiAwaitable)));
    } catch (...) {
      anyFailures.store(true, std::memory_order_relaxed);
      if (!cancelSource.requestCancellation()) {
        firstException = exception_wrapper{std::current_exception()};
      }
    }
  };

  // Create a task to await each input awaitable.
  std::vector<detail::BarrierTask> tasks;

  // TODO: Detect when the input range supports constant-time
  // .size() and pre-reserve storage for that many elements in 'tasks'.

  for (auto&& semiAwaitable : static_cast<InputRange&&>(awaitables)) {
    tasks.push_back(
        makeTask(static_cast<decltype(semiAwaitable)&&>(semiAwaitable)));
  }

  // Save the initial context and restore it after starting each task
  // as the task may have modified the context before suspending and we
  // want to make sure the next task is started with the same initial
  // context.
  const auto context = RequestContext::saveContext();

  auto& asyncFrame = co_await detail::co_current_async_stack_frame;

  // Launch the tasks and wait for them all to finish.
  {
    detail::Barrier barrier{tasks.size() + 1};
    for (auto&& task : tasks) {
      task.start(&barrier, asyncFrame);
      RequestContext::setContext(context);
    }
    co_await detail::UnsafeResumeInlineSemiAwaitable{barrier.arriveAndWait()};
  }

  // Check if there were any exceptions and rethrow the first one.
  if (anyFailures.load(std::memory_order_relaxed)) {
    if (firstException) {
      co_yield co_error(std::move(firstException));
    }
  }
}

template <typename InputRange>
auto collectAllTryRange(InputRange awaitables)
    -> folly::coro::Task<std::vector<detail::collect_all_try_range_component_t<
        detail::range_reference_t<InputRange>>>> {
  std::vector<detail::collect_all_try_range_component_t<
      detail::range_reference_t<InputRange>>>
      results;

  const folly::Executor::KeepAlive<> executor =
      folly::getKeepAliveToken(co_await co_current_executor);

  const CancellationToken& cancelToken = co_await co_current_cancellation_token;

  using awaitable_type = remove_cvref_t<detail::range_reference_t<InputRange>>;
  auto makeTask = [&](std::size_t index,
                      awaitable_type semiAwaitable) -> detail::BarrierTask {
    assert(index < results.size());
    auto& result = results[index];
    try {
      using await_result = semi_await_result_t<awaitable_type>;
      if constexpr (std::is_void_v<await_result>) {
        co_await co_viaIfAsync(
            executor.get_alias(),
            co_withCancellation(cancelToken, std::move(semiAwaitable)));
        result.emplace();
      } else {
        result.emplace(co_await co_viaIfAsync(
            executor.get_alias(),
            co_withCancellation(cancelToken, std::move(semiAwaitable))));
      }
    } catch (...) {
      result.emplaceException(std::current_exception());
    }
  };

  // Create a task to await each input awaitable.
  std::vector<detail::BarrierTask> tasks;

  // TODO: Detect when the input range supports constant-time
  // .size() and pre-reserve storage for that many elements in 'tasks'.

  {
    std::size_t index = 0;
    for (auto&& semiAwaitable : awaitables) {
      tasks.push_back(makeTask(
          index++, static_cast<decltype(semiAwaitable)&&>(semiAwaitable)));
    }
  }

  // Now that we know how many tasks there are, allocate that
  // many Try objects to store the results before we start
  // executing the tasks.
  results.resize(tasks.size());

  // Save the initial context and restore it after starting each task
  // as the task may have modified the context before suspending and we
  // want to make sure the next task is started with the same initial
  // context.
  const auto context = RequestContext::saveContext();

  auto& asyncFrame = co_await detail::co_current_async_stack_frame;

  // Launch the tasks and wait for them all to finish.
  {
    detail::Barrier barrier{tasks.size() + 1};
    for (auto&& task : tasks) {
      task.start(&barrier, asyncFrame);
      RequestContext::setContext(context);
    }
    co_await detail::UnsafeResumeInlineSemiAwaitable{barrier.arriveAndWait()};
  }

  co_return results;
}

template <
    typename InputRange,
    std::enable_if_t<
        std::is_void_v<
            semi_await_result_t<detail::range_reference_t<InputRange>>>,
        int>>
auto collectAllWindowed(InputRange awaitables, std::size_t maxConcurrency)
    -> folly::coro::Task<void> {
  assert(maxConcurrency > 0);

  const folly::Executor::KeepAlive<> executor = co_await co_current_executor;
  const folly::CancellationSource cancelSource;
  folly::CancellationCallback cancelCallback(
      co_await folly::coro::co_current_cancellation_token,
      [&]() noexcept { cancelSource.requestCancellation(); });
  const folly::CancellationToken cancelToken = cancelSource.getToken();

  exception_wrapper firstException;
  std::atomic<bool> anyFailures = false;

  const auto trySetFirstException = [&](exception_wrapper&& e) noexcept {
    anyFailures.store(true, std::memory_order_relaxed);
    if (!cancelSource.requestCancellation()) {
      // This is first entity to request cancellation.
      firstException = std::move(e);
    }
  };

  auto iter = access::begin(awaitables);
  const auto iterEnd = access::end(awaitables);

  using iterator_t = decltype(iter);
  using awaitable_t = typename std::iterator_traits<iterator_t>::value_type;

  folly::coro::Mutex mutex;

  exception_wrapper iterationException;

  auto makeWorker = [&]() -> detail::BarrierTask {
    auto lock =
        co_await co_viaIfAsync(executor.get_alias(), mutex.co_scoped_lock());

    while (!iterationException && iter != iterEnd) {
      std::optional<awaitable_t> awaitable;
      try {
        awaitable.emplace(*iter);
        ++iter;
      } catch (...) {
        iterationException = exception_wrapper{std::current_exception()};
        cancelSource.requestCancellation();
      }

      if (!awaitable) {
        co_return;
      }

      lock.unlock();

      try {
        co_await co_viaIfAsync(
            executor.get_alias(),
            co_withCancellation(cancelToken, std::move(*awaitable)));
      } catch (...) {
        trySetFirstException(exception_wrapper{std::current_exception()});
      }

      lock =
          co_await co_viaIfAsync(executor.get_alias(), mutex.co_scoped_lock());
    }
  };

  std::vector<detail::BarrierTask> workerTasks;

  detail::Barrier barrier{1};

  // Save the initial context and restore it after starting each task
  // as the task may have modified the context before suspending and we
  // want to make sure the next task is started with the same initial
  // context.
  const auto context = RequestContext::saveContext();

  auto& asyncFrame = co_await detail::co_current_async_stack_frame;

  try {
    auto lock = co_await mutex.co_scoped_lock();

    while (!iterationException && iter != iterEnd &&
           workerTasks.size() < maxConcurrency) {
      // Unlock the mutex before starting the worker so that
      // it can consume as many results synchronously as it can before
      // returning here and letting us spawn another task.
      // This can avoid spawning more worker coroutines than is necessary
      // to consume all of the awaitables.
      lock.unlock();

      workerTasks.push_back(makeWorker());
      barrier.add(1);
      workerTasks.back().start(&barrier, asyncFrame);

      RequestContext::setContext(context);

      lock = co_await mutex.co_scoped_lock();
    }
  } catch (...) {
    if (workerTasks.empty()) {
      iterationException = exception_wrapper{std::current_exception()};
    }
  }

  co_await detail::UnsafeResumeInlineSemiAwaitable{barrier.arriveAndWait()};

  if (iterationException) {
    co_yield co_error(std::move(iterationException));
  } else if (anyFailures.load(std::memory_order_relaxed)) {
    if (firstException) {
      co_yield co_error(std::move(firstException));
    }

    co_yield co_cancelled;
  }
}

template <
    typename InputRange,
    std::enable_if_t<
        !std::is_void_v<
            semi_await_result_t<detail::range_reference_t<InputRange>>>,
        int>>
auto collectAllWindowed(InputRange awaitables, std::size_t maxConcurrency)
    -> folly::coro::Task<std::vector<detail::collect_all_range_component_t<
        detail::range_reference_t<InputRange>>>> {
  assert(maxConcurrency > 0);

  const folly::Executor::KeepAlive<> executor = co_await co_current_executor;

  const folly::CancellationSource cancelSource;
  folly::CancellationCallback cancelCallback(
      co_await folly::coro::co_current_cancellation_token,
      [&]() noexcept { cancelSource.requestCancellation(); });
  const folly::CancellationToken cancelToken = cancelSource.getToken();

  exception_wrapper firstException;
  std::atomic<bool> anyFailures = false;

  auto trySetFirstException = [&](exception_wrapper&& e) noexcept {
    anyFailures.store(true, std::memory_order_relaxed);
    if (!cancelSource.requestCancellation()) {
      // This is first entity to request cancellation.
      firstException = std::move(e);
    }
  };

  auto iter = access::begin(awaitables);
  const auto iterEnd = access::end(awaitables);

  using iterator_t = decltype(iter);
  using awaitable_t = typename std::iterator_traits<iterator_t>::value_type;

  folly::coro::Mutex mutex;

  std::vector<detail::collect_all_try_range_component_t<
      detail::range_reference_t<InputRange>>>
      tryResults;

  exception_wrapper iterationException;

  auto makeWorker = [&]() -> detail::BarrierTask {
    auto lock =
        co_await co_viaIfAsync(executor.get_alias(), mutex.co_scoped_lock());

    while (!iterationException && iter != iterEnd) {
      const std::size_t thisIndex = tryResults.size();
      std::optional<awaitable_t> awaitable;
      try {
        tryResults.emplace_back();
        awaitable.emplace(*iter);
        ++iter;
      } catch (...) {
        iterationException = exception_wrapper{std::current_exception()};
        cancelSource.requestCancellation();
      }

      if (!awaitable) {
        co_return;
      }

      lock.unlock();

      detail::collect_all_try_range_component_t<
          detail::range_reference_t<InputRange>>
          tryResult;

      try {
        tryResult.emplace(co_await co_viaIfAsync(
            executor.get_alias(),
            co_withCancellation(
                cancelToken, static_cast<awaitable_t&&>(*awaitable))));
      } catch (...) {
        trySetFirstException(exception_wrapper{std::current_exception()});
      }

      lock =
          co_await co_viaIfAsync(executor.get_alias(), mutex.co_scoped_lock());

      try {
        tryResults[thisIndex] = std::move(tryResult);
      } catch (...) {
        trySetFirstException(exception_wrapper{std::current_exception()});
      }
    }
  };

  std::vector<detail::BarrierTask> workerTasks;

  detail::Barrier barrier{1};

  exception_wrapper workerCreationException;

  // Save the initial context and restore it after starting each task
  // as the task may have modified the context before suspending and we
  // want to make sure the next task is started with the same initial
  // context.
  const auto context = RequestContext::saveContext();

  auto& asyncFrame = co_await detail::co_current_async_stack_frame;

  try {
    auto lock = co_await mutex.co_scoped_lock();

    while (!iterationException && iter != iterEnd &&
           workerTasks.size() < maxConcurrency) {
      // Unlock the mutex before starting the worker so that
      // it can consume as many results synchronously as it can before
      // returning here and letting us spawn another task.
      // This can avoid spawning more worker coroutines than is necessary
      // to consume all of the awaitables.
      lock.unlock();

      workerTasks.push_back(makeWorker());
      barrier.add(1);
      workerTasks.back().start(&barrier, asyncFrame);

      RequestContext::setContext(context);

      lock = co_await mutex.co_scoped_lock();
    }
  } catch (...) {
    // Only a fatal error if we failed to create any worker tasks.
    if (workerTasks.empty()) {
      // No need to synchronise here. There are no concurrent tasks running.
      iterationException = exception_wrapper{std::current_exception()};
    }
  }

  co_await detail::UnsafeResumeInlineSemiAwaitable{barrier.arriveAndWait()};

  if (iterationException) {
    co_yield co_error(std::move(iterationException));
  } else if (anyFailures.load(std::memory_order_relaxed)) {
    if (firstException) {
      co_yield co_error(std::move(firstException));
    }

    // Otherwise, cancellation was requested before any of the child tasks
    // failed so complete with the OperationCancelled error.
    co_yield co_cancelled;
  }

  std::vector<detail::collect_all_range_component_t<
      detail::range_reference_t<InputRange>>>
      results;
  results.reserve(tryResults.size());

  for (auto&& tryResult : tryResults) {
    assert(tryResult.hasValue());
    results.emplace_back(std::move(tryResult).value());
  }

  co_return results;
}

template <typename InputRange>
auto collectAllTryWindowed(InputRange awaitables, std::size_t maxConcurrency)
    -> folly::coro::Task<std::vector<detail::collect_all_try_range_component_t<
        detail::range_reference_t<InputRange>>>> {
  assert(maxConcurrency > 0);

  std::vector<detail::collect_all_try_range_component_t<
      detail::range_reference_t<InputRange>>>
      results;

  exception_wrapper iterationException;

  folly::coro::Mutex mutex;

  const Executor::KeepAlive<> executor = co_await co_current_executor;
  const CancellationToken& cancelToken = co_await co_current_cancellation_token;

  auto iter = access::begin(awaitables);
  const auto iterEnd = access::end(awaitables);

  using iterator_t = decltype(iter);
  using awaitable_t = typename std::iterator_traits<iterator_t>::value_type;
  using result_t = semi_await_result_t<awaitable_t>;

  auto makeWorker = [&]() -> detail::BarrierTask {
    auto lock =
        co_await co_viaIfAsync(executor.get_alias(), mutex.co_scoped_lock());

    while (!iterationException && iter != iterEnd) {
      const std::size_t thisIndex = results.size();
      std::optional<awaitable_t> awaitable;

      try {
        results.emplace_back();
        awaitable.emplace(*iter);
        ++iter;
      } catch (...) {
        iterationException = exception_wrapper{std::current_exception()};
      }

      if (!awaitable) {
        co_return;
      }

      lock.unlock();

      detail::collect_all_try_range_component_t<
          detail::range_reference_t<InputRange>>
          result;

      try {
        if constexpr (std::is_void_v<result_t>) {
          co_await co_viaIfAsync(
              executor.get_alias(),
              co_withCancellation(cancelToken, std::move(*awaitable)));
          result.emplace();
        } else {
          result.emplace(co_await co_viaIfAsync(
              executor.get_alias(),
              co_withCancellation(cancelToken, std::move(*awaitable))));
        }
      } catch (...) {
        result.emplaceException(std::current_exception());
      }

      lock =
          co_await co_viaIfAsync(executor.get_alias(), mutex.co_scoped_lock());

      try {
        results[thisIndex] = std::move(result);
      } catch (...) {
        results[thisIndex].emplaceException(std::current_exception());
      }
    }
  };

  std::vector<detail::BarrierTask> workerTasks;

  detail::Barrier barrier{1};

  // Save the initial context and restore it after starting each task
  // as the task may have modified the context before suspending and we
  // want to make sure the next task is started with the same initial
  // context.
  const auto context = RequestContext::saveContext();

  auto& asyncFrame = co_await detail::co_current_async_stack_frame;

  try {
    auto lock = co_await mutex.co_scoped_lock();
    while (!iterationException && iter != iterEnd &&
           workerTasks.size() < maxConcurrency) {
      // Unlock the mutex before starting the child operation so that
      // it can consume as many results synchronously as it can before
      // returning here and letting us potentially spawn another task.
      // This can avoid spawning more worker coroutines than is necessary
      // to consume all of the awaitables.
      lock.unlock();

      workerTasks.push_back(makeWorker());
      barrier.add(1);
      workerTasks.back().start(&barrier, asyncFrame);

      RequestContext::setContext(context);

      lock = co_await mutex.co_scoped_lock();
    }
  } catch (...) {
    // Failure to create a worker is an error if we failed
    // to create _any_ workers. As long as we created one then
    // the algorithm should still be able to make forward progress.
    if (workerTasks.empty()) {
      iterationException = exception_wrapper{std::current_exception()};
    }
  }

  co_await detail::UnsafeResumeInlineSemiAwaitable{barrier.arriveAndWait()};

  if (iterationException) {
    co_yield co_error(std::move(iterationException));
  }

  co_return results;
}

template <typename InputRange>
auto makeUnorderedAsyncGeneratorFromAwaitableRange(
    AsyncScope& scope, InputRange awaitables)
    -> AsyncGenerator<detail::async_generator_from_awaitable_range_item_t<
        InputRange,
        false>&&> {
  return detail::makeUnorderedAsyncGeneratorFromAwaitableRangeImpl(
      scope, std::move(awaitables), bool_constant<false>{});
}

template <typename InputRange>
auto makeUnorderedAsyncGeneratorFromAwaitableTryRange(
    AsyncScope& scope, InputRange awaitables)
    -> AsyncGenerator<detail::async_generator_from_awaitable_range_item_t<
        InputRange,
        true>&&> {
  return detail::makeUnorderedAsyncGeneratorFromAwaitableRangeImpl(
      scope, std::move(awaitables), bool_constant<true>{});
}

template <typename SemiAwaitable, typename... SemiAwaitables>
auto collectAny(SemiAwaitable&& awaitable, SemiAwaitables&&... awaitables)
    -> folly::coro::Task<std::pair<
        std::size_t,
        folly::Try<detail::collect_any_component_t<
            SemiAwaitable,
            SemiAwaitables...>>>> {
  return detail::collectAnyImpl(
      std::make_index_sequence<sizeof...(SemiAwaitables) + 1>{},
      static_cast<SemiAwaitable&&>(awaitable),
      static_cast<SemiAwaitables&&>(awaitables)...);
}

template <typename... SemiAwaitables>
auto collectAnyNoDiscard(SemiAwaitables&&... awaitables)
    -> folly::coro::Task<std::tuple<detail::collect_all_try_component_t<
        remove_cvref_t<SemiAwaitables>>...>> {
  return detail::collectAnyNoDiscardImpl(
      std::make_index_sequence<sizeof...(SemiAwaitables)>{},
      static_cast<SemiAwaitables&&>(awaitables)...);
}

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
