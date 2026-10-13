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
// production collectors with await_resume_try() hidden on the input tasks.

#include <folly/Benchmark.h>
#include <folly/Try.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/Collect.h>
#include <folly/coro/Task.h>
#include <folly/coro/ViaIfAsync.h>

#include <type_traits>
#include <utility>
#include <vector>

#if FOLLY_HAS_COROUTINES

struct BenchmarkError : std::exception {};

folly::coro::Task<int> succeedingTask(int value) {
  co_return value;
}

folly::coro::Task<int> failingTask() {
  co_yield folly::coro::co_error(BenchmarkError{});
}

namespace {

template <typename Awaitable>
class WithoutTryAwaiter
    : public folly::coro::detail::ValueOnlyAwaiterBase<Awaitable> {
  using Base = folly::coro::detail::ValueOnlyAwaiterBase<Awaitable>;

 public:
  explicit WithoutTryAwaiter(Awaitable&& awaitable)
      : Base(static_cast<Awaitable&&>(awaitable)) {}

  auto await_resume() FOLLY_DETAIL_FORWARD_BODY(this->awaiter_.await_resume())
};

template <typename Awaitable>
class [[FOLLY_ATTR_CLANG_CORO_AWAIT_ELIDABLE]] WithoutTryAwaitable
    : public folly::coro::detail::
          CommutativeWrapperAwaitable<WithoutTryAwaitable, Awaitable> {
 public:
  using folly::coro::detail::CommutativeWrapperAwaitable<
      WithoutTryAwaitable,
      Awaitable>::CommutativeWrapperAwaitable;

  auto operator co_await() &&
    requires folly::coro::is_awaitable_v<Awaitable>
  {
    return WithoutTryAwaiter<Awaitable>{std::move(this->inner_)};
  }
};

using Task = folly::coro::Task<int>;
using TaskWithoutTry = WithoutTryAwaitable<Task>;
using Awaiter = folly::coro::semi_await_awaitable_t<Task>;
using AwaiterWithoutTry = folly::coro::semi_await_awaitable_t<TaskWithoutTry>;

static_assert(folly::coro::detail::collect_detail::can_co_awaitTry_v<Task>);
static_assert(
    !folly::coro::detail::collect_detail::can_co_awaitTry_v<TaskWithoutTry>);
static_assert(std::is_same_v<
              folly::coro::semi_await_result_t<TaskWithoutTry>,
              folly::coro::semi_await_result_t<Task>>);
static_assert(std::is_same_v<
              decltype(folly::coro::co_withAsyncStack(
                  std::declval<AwaiterWithoutTry>())),
              AwaiterWithoutTry>);
static_assert(sizeof(TaskWithoutTry) == sizeof(Task));
static_assert(sizeof(AwaiterWithoutTry) == sizeof(Awaiter));
static_assert(
    sizeof(folly::coro::awaiter_type_t<AwaiterWithoutTry>) == sizeof(Awaiter));

} // namespace

template <bool UseTryCatch>
auto makeBenchmarkTask(bool fail, int value) {
  auto task = fail ? failingTask() : succeedingTask(value);
  if constexpr (UseTryCatch) {
    return TaskWithoutTry{std::move(task)};
  } else {
    return task;
  }
}

template <bool UseTryCatch, size_t Failures, size_t... Indices>
void benchmarkCollectAllTry(size_t iters, std::index_sequence<Indices...>) {
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    for (size_t iter = 0; iter < iters; ++iter) {
      auto results = co_await folly::coro::collectAllTry(
          makeBenchmarkTask<UseTryCatch>(Indices < Failures, int(Indices))...);
      folly::doNotOptimizeAway(results);
    }
  }());
}

BENCHMARK(coAwaitTry_VariadicAllSucceed_8, iters) {
  benchmarkCollectAllTry<false, 0>(iters, std::make_index_sequence<8>{});
}
BENCHMARK_RELATIVE(tryCatch_VariadicAllSucceed_8, iters) {
  benchmarkCollectAllTry<true, 0>(iters, std::make_index_sequence<8>{});
}
BENCHMARK_DRAW_LINE();

BENCHMARK(coAwaitTry_VariadicAllFail_8, iters) {
  benchmarkCollectAllTry<false, 8>(iters, std::make_index_sequence<8>{});
}
BENCHMARK_RELATIVE(tryCatch_VariadicAllFail_8, iters) {
  benchmarkCollectAllTry<true, 8>(iters, std::make_index_sequence<8>{});
}
BENCHMARK_DRAW_LINE();

BENCHMARK(coAwaitTry_VariadicHalfFail_8, iters) {
  benchmarkCollectAllTry<false, 4>(iters, std::make_index_sequence<8>{});
}
BENCHMARK_RELATIVE(tryCatch_VariadicHalfFail_8, iters) {
  benchmarkCollectAllTry<true, 4>(iters, std::make_index_sequence<8>{});
}
BENCHMARK_DRAW_LINE();

BENCHMARK(coAwaitTry_VariadicSingleSuccess, iters) {
  benchmarkCollectAllTry<false, 0>(iters, std::make_index_sequence<1>{});
}
BENCHMARK_RELATIVE(tryCatch_VariadicSingleSuccess, iters) {
  benchmarkCollectAllTry<true, 0>(iters, std::make_index_sequence<1>{});
}
BENCHMARK_DRAW_LINE();

BENCHMARK(coAwaitTry_VariadicSingleFailure, iters) {
  benchmarkCollectAllTry<false, 1>(iters, std::make_index_sequence<1>{});
}
BENCHMARK_RELATIVE(tryCatch_VariadicSingleFailure, iters) {
  benchmarkCollectAllTry<true, 1>(iters, std::make_index_sequence<1>{});
}
BENCHMARK_DRAW_LINE();

template <
    bool UseTryCatch,
    size_t TaskCount,
    typename IsFailure,
    typename Collect>
void benchmarkCollectRange(size_t iters, IsFailure isFailure, Collect collect) {
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    for (size_t iter = 0; iter < iters; ++iter) {
      std::vector<decltype(makeBenchmarkTask<UseTryCatch>(false, 0))> tasks;
      tasks.reserve(TaskCount);
      for (size_t i = 0; i < TaskCount; ++i) {
        tasks.push_back(makeBenchmarkTask<UseTryCatch>(isFailure(i), int(i)));
      }
      auto results = co_await collect(std::move(tasks));
      folly::doNotOptimizeAway(results);
    }
  }());
}

template <bool UseTryCatch, size_t TaskCount, size_t Failures>
void benchmarkCollectAllTryWindowed(size_t iters) {
  benchmarkCollectRange<UseTryCatch, TaskCount>(
      iters,
      [](size_t index) { return index < Failures; },
      [](auto tasks) {
        return folly::coro::collectAllTryWindowed(std::move(tasks), 8);
      });
}

BENCHMARK(coAwaitTry_WindowedAllSucceed_100, iters) {
  benchmarkCollectAllTryWindowed<false, 100, 0>(iters);
}
BENCHMARK_RELATIVE(tryCatch_WindowedAllSucceed_100, iters) {
  benchmarkCollectAllTryWindowed<true, 100, 0>(iters);
}
BENCHMARK_DRAW_LINE();

BENCHMARK(coAwaitTry_WindowedAllFail_100, iters) {
  benchmarkCollectAllTryWindowed<false, 100, 100>(iters);
}
BENCHMARK_RELATIVE(tryCatch_WindowedAllFail_100, iters) {
  benchmarkCollectAllTryWindowed<true, 100, 100>(iters);
}
BENCHMARK_DRAW_LINE();

BENCHMARK(coAwaitTry_WindowedHalfFail_100, iters) {
  benchmarkCollectAllTryWindowed<false, 100, 50>(iters);
}
BENCHMARK_RELATIVE(tryCatch_WindowedHalfFail_100, iters) {
  benchmarkCollectAllTryWindowed<true, 100, 50>(iters);
}
BENCHMARK_DRAW_LINE();

BENCHMARK(coAwaitTry_WindowedSingleSuccess, iters) {
  benchmarkCollectAllTryWindowed<false, 1, 0>(iters);
}
BENCHMARK_RELATIVE(tryCatch_WindowedSingleSuccess, iters) {
  benchmarkCollectAllTryWindowed<true, 1, 0>(iters);
}
BENCHMARK_DRAW_LINE();

BENCHMARK(coAwaitTry_WindowedSingleFailure, iters) {
  benchmarkCollectAllTryWindowed<false, 1, 1>(iters);
}
BENCHMARK_RELATIVE(tryCatch_WindowedSingleFailure, iters) {
  benchmarkCollectAllTryWindowed<true, 1, 1>(iters);
}
BENCHMARK_DRAW_LINE();

template <bool UseTryCatch, size_t Failures>
void benchmarkCollectAllTryRange(size_t iters) {
  benchmarkCollectRange<UseTryCatch, 100>(
      iters,
      [](size_t index) {
        return Failures == 100 || (Failures == 50 && index % 2 != 0);
      },
      [](auto tasks) {
        return folly::coro::collectAllTryRange(std::move(tasks));
      });
}

BENCHMARK(coAwaitTry_AllSucceed_100, iters) {
  benchmarkCollectAllTryRange<false, 0>(iters);
}
BENCHMARK_RELATIVE(tryCatch_AllSucceed_100, iters) {
  benchmarkCollectAllTryRange<true, 0>(iters);
}
BENCHMARK_DRAW_LINE();

BENCHMARK(coAwaitTry_AllFail_100, iters) {
  benchmarkCollectAllTryRange<false, 100>(iters);
}
BENCHMARK_RELATIVE(tryCatch_AllFail_100, iters) {
  benchmarkCollectAllTryRange<true, 100>(iters);
}
BENCHMARK_DRAW_LINE();

BENCHMARK(coAwaitTry_HalfFail_100, iters) {
  benchmarkCollectAllTryRange<false, 50>(iters);
}
BENCHMARK_RELATIVE(tryCatch_HalfFail_100, iters) {
  benchmarkCollectAllTryRange<true, 50>(iters);
}
BENCHMARK_DRAW_LINE();

template <bool UseTryCatch, bool Fail>
void benchmarkSingleTask(size_t iters) {
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    for (size_t iter = 0; iter < iters; ++iter) {
      if constexpr (UseTryCatch) {
        folly::Try<int> result;
        try {
          result.emplace(co_await makeBenchmarkTask<true>(Fail, 42));
        } catch (...) {
          result.emplaceException(folly::current_exception());
        }
        folly::doNotOptimizeAway(result);
      } else {
        auto result = co_await folly::coro::co_awaitTry(
            makeBenchmarkTask<false>(Fail, 42));
        folly::doNotOptimizeAway(result);
      }
    }
  }());
}

BENCHMARK(coAwaitTry_SingleSuccess, iters) {
  benchmarkSingleTask<false, false>(iters);
}
BENCHMARK_RELATIVE(tryCatch_SingleSuccess, iters) {
  benchmarkSingleTask<true, false>(iters);
}
BENCHMARK_DRAW_LINE();

BENCHMARK(coAwaitTry_SingleFailure, iters) {
  benchmarkSingleTask<false, true>(iters);
}
BENCHMARK_RELATIVE(tryCatch_SingleFailure, iters) {
  benchmarkSingleTask<true, true>(iters);
}

#endif

int main(int argc, char** argv) {
  folly::gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
