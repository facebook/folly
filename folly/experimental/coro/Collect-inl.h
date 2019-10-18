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

#include <folly/ExceptionWrapper.h>
#include <folly/experimental/coro/Mutex.h>
#include <folly/experimental/coro/detail/Barrier.h>
#include <folly/experimental/coro/detail/BarrierTask.h>
#include <folly/experimental/coro/detail/Helpers.h>

namespace folly {
namespace coro {
namespace detail {

template <typename T>
T&& getValueOrUnit(Try<T>&& value) {
  return std::move(value).value();
}

inline Unit getValueOrUnit(Try<void>&& value) {
  value.throwIfFailed();
  return Unit{};
}

template <typename SemiAwaitable, typename Result>
detail::BarrierTask makeCollectAllTask(
    folly::Executor* executor,
    SemiAwaitable&& awaitable,
    const CancellationToken& cancelToken,
    const CancellationSource& cancelSource,
    Try<Result>& result) {
  try {
    if constexpr (std::is_void_v<Result>) {
      co_await co_viaIfAsync(
          executor,
          co_withCancellation(
              cancelToken, static_cast<SemiAwaitable&&>(awaitable)));
      result.emplace();
    } else {
      result.emplace(co_await co_viaIfAsync(
          executor,
          co_withCancellation(
              cancelToken, static_cast<SemiAwaitable&&>(awaitable))));
    }
  } catch (const std::exception& ex) {
    result.emplaceException(std::current_exception(), ex);
    cancelSource.requestCancellation();
  } catch (...) {
    result.emplaceException(std::current_exception());
    cancelSource.requestCancellation();
  }
}

template <typename... SemiAwaitables, size_t... Indices>
auto collectAllTryImpl(
    std::index_sequence<Indices...>,
    SemiAwaitables... awaitables)
    -> folly::coro::Task<
        std::tuple<collect_all_try_component_t<SemiAwaitables>...>> {
  static_assert(sizeof...(Indices) == sizeof...(SemiAwaitables));
  if constexpr (sizeof...(SemiAwaitables) == 0) {
    co_return std::tuple<>{};
  } else {
    std::tuple<collect_all_try_component_t<SemiAwaitables>...> results;

    Executor* executor = co_await co_current_executor;
    const CancellationToken& cancelToken =
        co_await co_current_cancellation_token;
    const CancellationSource cancelSource = CancellationSource::invalid();

    folly::coro::detail::BarrierTask tasks[sizeof...(SemiAwaitables)] = {
        makeCollectAllTask(
            executor,
            static_cast<SemiAwaitables&&>(awaitables),
            cancelToken,
            cancelSource,
            std::get<Indices>(results))...,
    };

    folly::coro::detail::Barrier barrier{sizeof...(SemiAwaitables) + 1};

    // Use std::initializer_list to ensure that the sub-tasks are launched
    // in the order they appear in the parameter pack.

    // Save the initial context and restore it after starting each task
    // as the task may have modified the context before suspending and we
    // want to make sure the next task is started with the same initial
    // context.
    const auto context = RequestContext::saveContext();
    (void)std::initializer_list<int>{
        (tasks[Indices].start(&barrier),
         RequestContext::setContext(context),
         0)...};

    // Wait for all of the sub-tasks to finish execution.
    // Should be safe to avoid an executor transition here even if the
    // operation completes asynchronously since all of the child tasks
    // should already have transitioned to the correct executor due to
    // the use of co_viaIfAsync() within makeBarrierTask().
    co_await UnsafeResumeInlineSemiAwaitable{barrier.arriveAndWait()};

    co_return results;
  }
}

template <typename... SemiAwaitables, size_t... Indices>
auto collectAllImpl(
    std::index_sequence<Indices...>,
    SemiAwaitables... awaitables)
    -> folly::coro::Task<
        std::tuple<collect_all_component_t<SemiAwaitables>...>> {
  if constexpr (sizeof...(SemiAwaitables) == 0) {
    co_return std::tuple<>{};
  } else {
    std::tuple<collect_all_try_component_t<SemiAwaitables>...> results;

    Executor* executor = co_await co_current_executor;

    const CancellationSource cancelSource;
    CancellationCallback cancelCallback(
        co_await co_current_cancellation_token, [&]() noexcept {
          cancelSource.requestCancellation();
        });
    const CancellationToken cancelToken = cancelSource.getToken();

    folly::coro::detail::BarrierTask tasks[sizeof...(SemiAwaitables)] = {
        makeCollectAllTask(
            executor,
            static_cast<SemiAwaitables&&>(awaitables),
            cancelToken,
            cancelSource,
            std::get<Indices>(results))...,
    };

    folly::coro::detail::Barrier barrier{sizeof...(SemiAwaitables) + 1};

    // Save the initial context and restore it after starting each task
    // as the task may have modified the context before suspending and we
    // want to make sure the next task is started with the same initial
    // context.
    const auto context = RequestContext::saveContext();

    // Use std::initializer_list to ensure that the sub-tasks are launched
    // in the order they appear in the parameter pack.
    (void)std::initializer_list<int>{
        (tasks[Indices].start(&barrier),
         RequestContext::setContext(context),
         0)...};

    // Wait for all of the sub-tasks to finish execution.
    // Should be safe to avoid an executor transition here even if the
    // operation completes asynchronously since all of the child tasks
    // should already have transitioned to the correct executor due to
    // the use of co_viaIfAsync() within makeBarrierTask().
    co_await UnsafeResumeInlineSemiAwaitable{barrier.arriveAndWait()};

    co_return std::tuple<collect_all_component_t<SemiAwaitables>...>{
        getValueOrUnit(std::get<Indices>(std::move(results)))...};
  }
}

template <typename InputRange>
auto collectAllTryRangeImpl(
    InputRange awaitables,
    CancellationToken cancelToken,
    CancellationSource cancelSource)
    -> folly::coro::Task<std::vector<detail::collect_all_try_range_component_t<
        detail::range_reference_t<InputRange>>>> {
  std::vector<detail::collect_all_try_range_component_t<
      detail::range_reference_t<InputRange>>>
      results;

  folly::Executor::KeepAlive<> executor =
      folly::getKeepAliveToken(co_await co_current_executor);

  using awaitable_type = remove_cvref_t<detail::range_reference_t<InputRange>>;
  auto makeTask = [&](std::size_t index,
                      awaitable_type semiAwaitable) -> detail::BarrierTask {
    assert(index < results.size());
    auto& result = results[index];
    try {
      using await_result =
          semi_await_result_t<detail::range_reference_t<InputRange>>;
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
    } catch (const std::exception& ex) {
      result.emplaceException(std::current_exception(), ex);
      cancelSource.requestCancellation();
    } catch (...) {
      result.emplaceException(std::current_exception());
      cancelSource.requestCancellation();
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

  // Launch the tasks and wait for them all to finish.
  {
    detail::Barrier barrier{tasks.size() + 1};
    for (auto&& task : tasks) {
      task.start(&barrier);
      RequestContext::setContext(context);
    }
    co_await detail::UnsafeResumeInlineSemiAwaitable{barrier.arriveAndWait()};
  }

  co_return results;
}

template <typename InputRange>
auto collectAllTryWindowedImpl(
    InputRange awaitables,
    std::size_t maxConcurrency,
    CancellationToken cancelToken,
    CancellationSource cancelSource)
    -> folly::coro::Task<std::vector<detail::collect_all_try_range_component_t<
        detail::range_reference_t<InputRange>>>> {
  assert(maxConcurrency > 0);

  std::vector<detail::collect_all_try_range_component_t<
      detail::range_reference_t<InputRange>>>
      results;

  exception_wrapper iterationException;

  folly::coro::Mutex mutex;

  folly::Executor::KeepAlive<> executor =
      folly::getKeepAliveToken(co_await co_current_executor);

  using std::begin;
  using std::end;
  auto iter = begin(awaitables);
  const auto iterEnd = end(awaitables);

  using iterator_t = decltype(iter);
  using awaitable_t = typename std::iterator_traits<iterator_t>::value_type;
  using result_t = semi_await_result_t<awaitable_t>;

  auto makeWorker = [&]() -> detail::BarrierTask {
    auto lock =
        co_await co_viaIfAsync(executor.get_alias(), mutex.co_scoped_lock());

    while (!iterationException && iter != iterEnd) {
      try {
        awaitable_t awaitable = *iter;

        try {
          ++iter;
        } catch (const std::exception& ex) {
          iterationException = exception_wrapper{std::current_exception(), ex};
          cancelSource.requestCancellation();
        } catch (...) {
          iterationException = exception_wrapper{std::current_exception()};
          cancelSource.requestCancellation();
        }

        const auto thisIndex = results.size();
        try {
          results.emplace_back();
        } catch (const std::exception& ex) {
          if (!iterationException) {
            iterationException =
                exception_wrapper{std::current_exception(), ex};
            cancelSource.requestCancellation();
          }

          // Failure to grow the results vector is fatal.
          co_return;
        } catch (...) {
          if (!iterationException) {
            iterationException = exception_wrapper{std::current_exception()};
            cancelSource.requestCancellation();
          }

          // Failure to grow the results vector is fatal.
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
                co_withCancellation(
                    cancelToken, static_cast<awaitable_t&&>(awaitable)));
            result.emplace();
          } else {
            result.emplace(co_await co_viaIfAsync(
                executor.get_alias(),
                co_withCancellation(
                    cancelToken, static_cast<awaitable_t&&>(awaitable))));
          }
        } catch (const std::exception& ex) {
          result.emplaceException(std::current_exception(), ex);
          cancelSource.requestCancellation();
        } catch (...) {
          result.emplaceException(std::current_exception());
          cancelSource.requestCancellation();
        }

        lock = co_await co_viaIfAsync(
            executor.get_alias(), mutex.co_scoped_lock());

        try {
          results[thisIndex] = std::move(result);
        } catch (const std::exception& ex) {
          results[thisIndex].emplaceException(std::current_exception(), ex);
          cancelSource.requestCancellation();
        } catch (...) {
          results[thisIndex].emplaceException(std::current_exception());
          cancelSource.requestCancellation();
        }
      } catch (const std::exception& ex) {
        assert(lock.owns_lock());
        if (!iterationException) {
          iterationException = exception_wrapper{std::current_exception(), ex};
          cancelSource.requestCancellation();
        }
        co_return;
      } catch (...) {
        assert(lock.owns_lock());
        if (!iterationException) {
          iterationException = exception_wrapper{std::current_exception()};
          cancelSource.requestCancellation();
        }
        co_return;
      }

      assert(lock.owns_lock());
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

  try {
    auto lock = co_await mutex.co_scoped_lock();
    while (iter != iterEnd && workerTasks.size() < maxConcurrency) {
      // Unlock the mutex before starting the child operation so that
      // it can consume as many results synchronously as it can before
      // returning here and letting us potentially spawn another task.
      // This can avoid spawning more worker coroutines than is necessary
      // to consume all of the awaitables.
      lock.unlock();

      workerTasks.push_back(makeWorker());
      barrier.add(1);
      workerTasks.back().start(&barrier);

      RequestContext::setContext(context);

      lock = co_await mutex.co_scoped_lock();
    }
  } catch (const std::exception& ex) {
    workerCreationException = exception_wrapper{std::current_exception(), ex};
    cancelSource.requestCancellation();
  } catch (...) {
    workerCreationException = exception_wrapper{std::current_exception()};
    cancelSource.requestCancellation();
  }

  co_await detail::UnsafeResumeInlineSemiAwaitable{barrier.arriveAndWait()};

  if (iterationException) {
    co_return folly::Try<decltype(results)>{std::move(iterationException)};
  } else if (workerTasks.empty() && workerCreationException) {
    // Couldn't spawn any child workers to execute the tasks.
    // TODO: We could update this code-path to try to execute them serially
    // here but the system is probably in a bad state anyway.
    co_return folly::Try<decltype(results)>{std::move(workerCreationException)};
  }

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
  const CancellationSource cancelSource;
  CancellationCallback cancelCallback(
      co_await co_current_cancellation_token, [&]() noexcept {
        cancelSource.requestCancellation();
      });

  auto resultsTry =
      co_await folly::coro::co_awaitTry(detail::collectAllTryRangeImpl(
          std::move(awaitables), cancelSource.getToken(), cancelSource));

  if (resultsTry.hasException()) {
    co_return Try<std::vector<detail::collect_all_range_component_t<
        detail::range_reference_t<InputRange>>>>{
        std::move(resultsTry.exception())};
  }

  auto& results = resultsTry.value();

  // Collate the results into a single result vector.
  std::vector<detail::collect_all_range_component_t<
      detail::range_reference_t<InputRange>>>
      values;
  values.reserve(results.size());
  for (auto&& result : results) {
    values.push_back(std::move(result).value());
  }

  co_return values;
}

template <
    typename InputRange,
    std::enable_if_t<
        std::is_void_v<
            semi_await_result_t<detail::range_reference_t<InputRange>>>,
        int>>
auto collectAllRange(InputRange awaitables) -> folly::coro::Task<void> {
  folly::Executor::KeepAlive<> executor =
      folly::getKeepAliveToken(co_await co_current_executor);

  const CancellationSource cancelSource;
  CancellationCallback cancelCallback(
      co_await co_current_cancellation_token, [&]() noexcept {
        cancelSource.requestCancellation();
      });
  const CancellationToken cancelToken = cancelSource.getToken();

  std::atomic<bool> anyFailures{false};
  exception_wrapper firstException;

  using awaitable_type = remove_cvref_t<detail::range_reference_t<InputRange>>;
  auto makeTask = [&](awaitable_type semiAwaitable) -> detail::BarrierTask {
    try {
      co_await co_viaIfAsync(
          executor.get_alias(),
          co_withCancellation(cancelToken, std::move(semiAwaitable)));
    } catch (const std::exception& ex) {
      if (!anyFailures.exchange(true, std::memory_order_relaxed)) {
        firstException = exception_wrapper{std::current_exception(), ex};
        cancelSource.requestCancellation();
      }
    } catch (...) {
      if (!anyFailures.exchange(true, std::memory_order_relaxed)) {
        firstException = exception_wrapper{std::current_exception()};
        cancelSource.requestCancellation();
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

  // Launch the tasks and wait for them all to finish.
  {
    detail::Barrier barrier{tasks.size() + 1};
    for (auto&& task : tasks) {
      task.start(&barrier);
      RequestContext::setContext(context);
    }
    co_await detail::UnsafeResumeInlineSemiAwaitable{barrier.arriveAndWait()};
  }

  // Check if there were any exceptions and rethrow the first one.
  if (anyFailures.load(std::memory_order_relaxed)) {
    firstException.throw_exception();
  }
}

template <typename InputRange>
auto collectAllTryRange(InputRange awaitables)
    -> folly::coro::Task<std::vector<detail::collect_all_try_range_component_t<
        detail::range_reference_t<InputRange>>>> {
  co_return co_await detail::collectAllTryRangeImpl(
      std::move(awaitables),
      co_await co_current_cancellation_token,
      CancellationSource::invalid());
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

  exception_wrapper firstException;

  folly::coro::Mutex mutex;

  folly::Executor::KeepAlive<> executor =
      folly::getKeepAliveToken(co_await co_current_executor);

  const folly::CancellationSource cancelSource;
  folly::CancellationCallback cancelCallback(
      co_await co_current_cancellation_token, [&]() noexcept {
        cancelSource.requestCancellation();
      });
  const folly::CancellationToken cancelToken = cancelSource.getToken();

  using std::begin;
  using std::end;
  auto iter = begin(awaitables);
  const auto iterEnd = end(awaitables);

  using iterator_t = decltype(iter);
  using awaitable_t = typename std::iterator_traits<iterator_t>::value_type;

  auto makeWorker = [&]() -> detail::BarrierTask {
    auto lock =
        co_await co_viaIfAsync(executor.get_alias(), mutex.co_scoped_lock());

    while (iter != iterEnd) {
      awaitable_t awaitable = *iter;
      try {
        ++iter;
      } catch (const std::exception& ex) {
        if (!firstException) {
          firstException = exception_wrapper{std::current_exception(), ex};
          cancelSource.requestCancellation();
        }
      } catch (...) {
        if (!firstException) {
          firstException = exception_wrapper{std::current_exception()};
          cancelSource.requestCancellation();
        }
      }

      lock.unlock();

      exception_wrapper ex;
      try {
        co_await co_viaIfAsync(
            executor.get_alias(),
            co_withCancellation(
                cancelToken, static_cast<awaitable_t&&>(awaitable)));
      } catch (const std::exception& e) {
        ex = exception_wrapper{std::current_exception(), e};
      } catch (...) {
        ex = exception_wrapper{std::current_exception()};
      }

      lock =
          co_await co_viaIfAsync(executor.get_alias(), mutex.co_scoped_lock());

      if (ex && !firstException) {
        firstException = std::move(ex);
        cancelSource.requestCancellation();
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

  try {
    auto lock = co_await mutex.co_scoped_lock();

    while (iter != iterEnd && workerTasks.size() < maxConcurrency) {
      // Unlock the mutex before starting the worker so that
      // it can consume as many results synchronously as it can before
      // returning here and letting us spawn another task.
      // This can avoid spawning more worker coroutines than is necessary
      // to consume all of the awaitables.
      lock.unlock();

      workerTasks.push_back(makeWorker());
      barrier.add(1);
      workerTasks.back().start(&barrier);

      RequestContext::setContext(context);

      lock = co_await mutex.co_scoped_lock();
    }
  } catch (const std::exception& ex) {
    workerCreationException = exception_wrapper{std::current_exception(), ex};
    cancelSource.requestCancellation();
  } catch (...) {
    workerCreationException = exception_wrapper{std::current_exception()};
    cancelSource.requestCancellation();
  }

  co_await detail::UnsafeResumeInlineSemiAwaitable{barrier.arriveAndWait()};

  if (firstException) {
    firstException.throw_exception();
  } else if (workerTasks.empty() && workerCreationException) {
    // Failed to create any workers to process the tasks.
    workerCreationException.throw_exception();
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

  const folly::CancellationSource cancelSource;
  folly::CancellationCallback cancelCallback(
      co_await co_current_cancellation_token, [&]() noexcept {
        cancelSource.requestCancellation();
      });
  const folly::CancellationToken cancelToken = cancelSource.getToken();

  auto tryResults = co_await detail::collectAllTryWindowedImpl(
      std::move(awaitables),
      maxConcurrency,
      cancelSource.getToken(),
      cancelSource);

  std::vector<detail::collect_all_range_component_t<
      detail::range_reference_t<InputRange>>>
      results;
  results.reserve(tryResults.size());

  for (auto&& tryResult : tryResults) {
    if (tryResult.hasException()) {
      co_return folly::Try<std::vector<detail::collect_all_range_component_t<
          detail::range_reference_t<InputRange>>>>{
          std::move(tryResult.exception())};
    }
    results.emplace_back(std::move(tryResult).value());
  }

  co_return results;
}

template <typename InputRange>
auto collectAllTryWindowed(InputRange awaitables, std::size_t maxConcurrency)
    -> folly::coro::Task<std::vector<detail::collect_all_try_range_component_t<
        detail::range_reference_t<InputRange>>>> {
  co_return co_await detail::collectAllTryWindowedImpl(
      std::move(awaitables),
      maxConcurrency,
      co_await co_current_cancellation_token,
      CancellationSource::invalid());
}

} // namespace coro
} // namespace folly
