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

#include <folly/Benchmark.h>
#include <folly/Portability.h>
#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/Collect.h>
#include <folly/experimental/coro/FutureUtil.h>
#include <folly/experimental/coro/Promise.h>
#include <folly/experimental/coro/Task.h>
#include <folly/futures/Future.h>

#if FOLLY_HAS_COROUTINES

void resetMallocStats() {
  BENCHMARK_SUSPEND {
    static uint64_t epoch = 0;
    ++epoch;
    size_t sz = sizeof(epoch);
    mallctl("epoch", &epoch, &sz, &epoch, sz);
  };
}
void setMallocStats(folly::UserCounters& counters) {
  BENCHMARK_SUSPEND {
    size_t allocated = 0;
    size_t sz = sizeof(allocated);
    mallctl("stats.allocated", &allocated, &sz, nullptr, 0);
    counters["allocated"] = allocated;
  };
}

BENCHMARK_COUNTERS(CoroFutureImmediateUnwrapped, counters, iters) {
  resetMallocStats();

  for (std::size_t i = 0; i < iters; ++i) {
    auto [promise, future] = folly::coro::makePromiseContract<int>();
    promise.setValue(42);
    folly::coro::blockingWait(std::move(future));
  }

  setMallocStats(counters);
}

// You can't directly blockingWait a SemiFuture (it deadlocks) so there's no
// comparison for this one

BENCHMARK_COUNTERS(CoroFutureImmediate, counters, iters) {
  resetMallocStats();

  for (std::size_t i = 0; i < iters; ++i) {
    auto [promise, future] = folly::coro::makePromiseContract<int>();
    promise.setValue(42);
    folly::coro::blockingWait(folly::coro::toTask(std::move(future)));
  }

  setMallocStats(counters);
}

BENCHMARK_COUNTERS(FuturesFutureImmediate, counters, iters) {
  resetMallocStats();

  for (std::size_t i = 0; i < iters; ++i) {
    auto [promise, future] = folly::makePromiseContract<int>();
    promise.setValue(42);
    folly::coro::blockingWait(folly::coro::toTask(std::move(future)));
  }

  setMallocStats(counters);
}

BENCHMARK_COUNTERS(CoroFutureSuspend, counters, iters) {
  resetMallocStats();

  for (std::size_t i = 0; i < iters; ++i) {
    auto [this_promise, this_future] = folly::coro::makePromiseContract<int>();
    auto waiter = [](auto future) -> folly::coro::Task<int> {
      co_return co_await std::move(future);
    }(std::move(this_future));
    auto fulfiller = [](auto promise) -> folly::coro::Task<> {
      promise.setValue(42);
      co_return;
    }(std::move(this_promise));

    folly::coro::blockingWait(folly::coro::collectAll(
        co_awaitTry(std::move(waiter)), std::move(fulfiller)));
  }

  setMallocStats(counters);
}

BENCHMARK_COUNTERS(FuturesFutureSuspend, counters, iters) {
  resetMallocStats();

  for (std::size_t i = 0; i < iters; ++i) {
    auto [this_promise, this_future] = folly::makePromiseContract<int>();
    auto waiter = [](auto future) -> folly::coro::Task<int> {
      co_return co_await std::move(future);
    }(std::move(this_future));
    auto fulfiller = [](auto promise) -> folly::coro::Task<> {
      promise.setValue(42);
      co_return;
    }(std::move(this_promise));

    folly::coro::blockingWait(folly::coro::collectAll(
        co_awaitTry(std::move(waiter)), std::move(fulfiller)));
  }

  setMallocStats(counters);
}

#endif

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
