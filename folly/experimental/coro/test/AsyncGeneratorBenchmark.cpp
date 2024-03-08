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

#include <folly/experimental/coro/AsyncGenerator.h>
#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/Generator.h>
#include <folly/experimental/coro/Task.h>
#include <folly/experimental/coro/ViaIfAsync.h>

#include <folly/ExceptionWrapper.h>

#include <exception>

#if FOLLY_HAS_COROUTINES

struct SomeError : std::exception {};

BENCHMARK(asyncGeneratorThrowError, iters) {
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    for (size_t iter = 0; iter < iters; ++iter) {
      auto gen = []() -> folly::coro::AsyncGenerator<int> {
        co_yield 42;
        throw SomeError{};
      }();

      auto item1 = co_await gen.next();
      try {
        auto item2 = co_await gen.next();
        std::terminate();
      } catch (const SomeError&) {
      }
    }
  }());
}

BENCHMARK(asyncGeneratorThrowErrorAwaitTry, iters) {
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    for (size_t iter = 0; iter < iters; ++iter) {
      auto gen = []() -> folly::coro::AsyncGenerator<int> {
        co_yield 42;
        throw SomeError{};
      }();

      auto try1 = co_await folly::coro::co_awaitTry(gen.next());
      auto try2 = co_await folly::coro::co_awaitTry(gen.next());
      if (!try2.hasException() ||
          !try2.exception().is_compatible_with<SomeError>()) {
        std::terminate();
      }
    }
  }());
}

BENCHMARK(asyncGeneratorYieldError, iters) {
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    for (size_t iter = 0; iter < iters; ++iter) {
      auto gen = []() -> folly::coro::AsyncGenerator<int> {
        co_yield 42;
        co_yield folly::coro::co_error(SomeError{});
      }();

      auto item1 = co_await gen.next();
      try {
        auto item2 = co_await gen.next();
        std::terminate();
      } catch (const SomeError&) {
      }
    }
  }());
}

BENCHMARK(asyncGeneratorYieldErrorAwaitTry, iters) {
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    for (size_t iter = 0; iter < iters; ++iter) {
      auto gen = []() -> folly::coro::AsyncGenerator<int> {
        co_yield 42;
        co_yield folly::coro::co_error(SomeError{});
      }();

      auto try1 = co_await folly::coro::co_awaitTry(gen.next());
      auto try2 = co_await folly::coro::co_awaitTry(gen.next());
      if (!try2.hasException() ||
          !try2.exception().is_compatible_with<SomeError>()) {
        std::terminate();
      }
    }
  }());
}

/*
Comparing just the "hot paths" of the two generator coroutines:

11/08/23 16:25$ buck2 run @mode/opt \
  //folly/experimental/coro/test:async_generator_bench -- \
    -bm_regex '.*YieldValue.*'
============================================================================
[...]coro/test/AsyncGeneratorBenchmark.cpp     relative  time/iter   iters/s
============================================================================
asyncGeneratorYieldValues                                  12.00ns    83.32M
compareToSynchronousGeneratorYieldValues                    4.43ns   225.72M
*/

BENCHMARK(asyncGeneratorYieldValues, iters) {
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    auto gen = [](size_t iters) -> folly::coro::AsyncGenerator<size_t> {
      for (size_t iter = 0; iter < iters; ++iter) {
        co_yield iter;
      }
    }(iters);
    size_t i = 0;
    while (auto it = co_await gen.next()) {
      CHECK_EQ(i++, *it);
    }
  }());
}

BENCHMARK(compareToSynchronousGeneratorYieldValues, iters) {
  auto gen = [](size_t iters) -> folly::coro::Generator<size_t> {
    for (size_t iter = 0; iter < iters; ++iter) {
      co_yield iter;
    }
  }(iters);
  size_t i = 0;
  for (size_t iter : gen) {
    CHECK_EQ(i++, iter);
  }
}

#endif

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
