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

// Benchmark comparing co_awaitTry vs try/catch for collecting results from
// multiple coroutines into folly::Try values. The try/catch baseline uses the
// same barrier/scheduling machinery as the real collectAllTryRange, matching
// the original implementation before it was converted to co_awaitTry.

#include <folly/Benchmark.h>
#include <folly/Try.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/Collect.h>
#include <folly/coro/Task.h>
#include <folly/coro/ViaIfAsync.h>
#include <folly/coro/detail/Barrier.h>
#include <folly/coro/detail/BarrierTask.h>
#include <folly/coro/detail/CurrentAsyncFrame.h>
#include <folly/coro/detail/Helpers.h>
#include <folly/io/async/Request.h>

#include <vector>

#if FOLLY_HAS_COROUTINES

struct BenchmarkError : std::exception {};

folly::coro::Task<int> succeedingTask(int value) {
  co_return value;
}

folly::coro::Task<int> failingTask() {
  co_yield folly::coro::co_error(BenchmarkError{});
}

// Old collectAllTryRange implementation using try/catch (before the
// co_awaitTry conversion). Uses the same barrier machinery as the real
// implementation for an apples-to-apples comparison.
namespace folly::coro {

namespace benchmark_detail {
template <typename InputRange>
auto collectAllTryRangeWithTryCatchImpl(InputRange awaitables)
    -> Task<std::vector<detail::collect_all_try_range_component_t<
        detail::range_reference_t<InputRange>>>> {
  std::vector<detail::collect_all_try_range_component_t<
      detail::range_reference_t<InputRange>>>
      results;

  const folly::Executor::KeepAlive<> executor =
      folly::getKeepAliveToken(co_await co_current_executor);

  const CancellationToken& cancelToken = co_await co_current_cancellation_token;

  using awaitable_type = remove_cvref_t<detail::range_reference_t<InputRange>>;
  auto makeTask = [&](awaitable_type semiAwaitable, std::size_t index)
      -> detail::BarrierTask {
    assert(index < results.size());
    auto& result = results[index];
    // Original try/catch pattern
    try {
      using await_result = semi_await_result_t<awaitable_type>;
      if constexpr (std::is_void_v<await_result>) {
        co_await co_viaIfAsync(
            executor.get_alias(),
            co_withCancellation(cancelToken, std::move(semiAwaitable)));
        result.emplace();
      } else {
        result.emplace(
            co_await co_viaIfAsync(
                executor.get_alias(),
                co_withCancellation(cancelToken, std::move(semiAwaitable))));
      }
    } catch (...) {
      result.emplaceException(current_exception());
    }
  };

  auto tasks = detail::collectMakeInnerTaskVec(awaitables, makeTask);

  results.resize(tasks.size());

  const auto context = RequestContext::saveContext();
  auto& asyncFrame = co_await detail::co_current_async_stack_frame;

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
} // namespace benchmark_detail

// std::vector overload that wraps with MoveRange (mirrors the real API)
template <typename SemiAwaitable>
auto collectAllTryRangeWithTryCatch(std::vector<SemiAwaitable> awaitables)
    -> decltype(benchmark_detail::collectAllTryRangeWithTryCatchImpl(
        detail::MoveRange(awaitables))) {
  co_return co_await benchmark_detail::collectAllTryRangeWithTryCatchImpl(
      detail::MoveRange(awaitables));
}

} // namespace folly::coro

// ---------------------------------------------------------------------------
// All tasks succeed (100 tasks)
// ---------------------------------------------------------------------------
BENCHMARK(coAwaitTry_AllSucceed_100, iters) {
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    for (size_t iter = 0; iter < iters; ++iter) {
      std::vector<folly::coro::Task<int>> tasks;
      tasks.reserve(100);
      for (int i = 0; i < 100; ++i) {
        tasks.push_back(succeedingTask(i));
      }
      auto results = co_await folly::coro::collectAllTryRange(std::move(tasks));
      folly::doNotOptimizeAway(results);
    }
  }());
}

BENCHMARK_RELATIVE(tryCatch_AllSucceed_100, iters) {
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    for (size_t iter = 0; iter < iters; ++iter) {
      std::vector<folly::coro::Task<int>> tasks;
      tasks.reserve(100);
      for (int i = 0; i < 100; ++i) {
        tasks.push_back(succeedingTask(i));
      }
      auto results = co_await folly::coro::collectAllTryRangeWithTryCatch(
          std::move(tasks));
      folly::doNotOptimizeAway(results);
    }
  }());
}

BENCHMARK_DRAW_LINE();

// ---------------------------------------------------------------------------
// All tasks fail (100 tasks)
// ---------------------------------------------------------------------------
BENCHMARK(coAwaitTry_AllFail_100, iters) {
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    for (size_t iter = 0; iter < iters; ++iter) {
      std::vector<folly::coro::Task<int>> tasks;
      tasks.reserve(100);
      for (int i = 0; i < 100; ++i) {
        tasks.push_back(failingTask());
      }
      auto results = co_await folly::coro::collectAllTryRange(std::move(tasks));
      folly::doNotOptimizeAway(results);
    }
  }());
}

BENCHMARK_RELATIVE(tryCatch_AllFail_100, iters) {
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    for (size_t iter = 0; iter < iters; ++iter) {
      std::vector<folly::coro::Task<int>> tasks;
      tasks.reserve(100);
      for (int i = 0; i < 100; ++i) {
        tasks.push_back(failingTask());
      }
      auto results = co_await folly::coro::collectAllTryRangeWithTryCatch(
          std::move(tasks));
      folly::doNotOptimizeAway(results);
    }
  }());
}

BENCHMARK_DRAW_LINE();

// ---------------------------------------------------------------------------
// 50% tasks fail (100 tasks)
// ---------------------------------------------------------------------------
BENCHMARK(coAwaitTry_HalfFail_100, iters) {
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    for (size_t iter = 0; iter < iters; ++iter) {
      std::vector<folly::coro::Task<int>> tasks;
      tasks.reserve(100);
      for (int i = 0; i < 100; ++i) {
        if (i % 2 == 0) {
          tasks.push_back(succeedingTask(i));
        } else {
          tasks.push_back(failingTask());
        }
      }
      auto results = co_await folly::coro::collectAllTryRange(std::move(tasks));
      folly::doNotOptimizeAway(results);
    }
  }());
}

BENCHMARK_RELATIVE(tryCatch_HalfFail_100, iters) {
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    for (size_t iter = 0; iter < iters; ++iter) {
      std::vector<folly::coro::Task<int>> tasks;
      tasks.reserve(100);
      for (int i = 0; i < 100; ++i) {
        if (i % 2 == 0) {
          tasks.push_back(succeedingTask(i));
        } else {
          tasks.push_back(failingTask());
        }
      }
      auto results = co_await folly::coro::collectAllTryRangeWithTryCatch(
          std::move(tasks));
      folly::doNotOptimizeAway(results);
    }
  }());
}

BENCHMARK_DRAW_LINE();

// ---------------------------------------------------------------------------
// Single task (isolate per-task overhead)
// ---------------------------------------------------------------------------
BENCHMARK(coAwaitTry_SingleSuccess, iters) {
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    for (size_t iter = 0; iter < iters; ++iter) {
      auto result = co_await folly::coro::co_awaitTry(succeedingTask(42));
      folly::doNotOptimizeAway(result);
    }
  }());
}

BENCHMARK_RELATIVE(tryCatch_SingleSuccess, iters) {
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    for (size_t iter = 0; iter < iters; ++iter) {
      folly::Try<int> result;
      try {
        result.emplace(co_await succeedingTask(42));
      } catch (...) {
        result.emplaceException(folly::current_exception());
      }
      folly::doNotOptimizeAway(result);
    }
  }());
}

BENCHMARK_DRAW_LINE();

BENCHMARK(coAwaitTry_SingleFailure, iters) {
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    for (size_t iter = 0; iter < iters; ++iter) {
      auto result = co_await folly::coro::co_awaitTry(failingTask());
      folly::doNotOptimizeAway(result);
    }
  }());
}

BENCHMARK_RELATIVE(tryCatch_SingleFailure, iters) {
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    for (size_t iter = 0; iter < iters; ++iter) {
      folly::Try<int> result;
      try {
        result.emplace(co_await failingTask());
      } catch (...) {
        result.emplaceException(folly::current_exception());
      }
      folly::doNotOptimizeAway(result);
    }
  }());
}

#endif

int main(int argc, char** argv) {
  folly::gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
