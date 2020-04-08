/*
 * Copyright 2018-present Facebook, Inc.
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

#include <folly/Benchmark.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/Collect.h>
#include <folly/experimental/coro/Generator.h>
#include <folly/experimental/coro/Task.h>
#include <folly/synchronization/Baton.h>

void doWork() {}

folly::CPUThreadPoolExecutor executor(4);

void collectAllFuture(size_t batchSize) {
  std::vector<folly::Future<folly::Unit>> futures;
  for (size_t i = 0; i < batchSize; ++i) {
    futures.emplace_back(folly::via(&executor, [] { doWork(); }));
  }
  folly::collectAll(std::move(futures)).get();
}

void collectAllFutureInline(size_t batchSize) {
  std::vector<folly::Future<folly::Unit>> futures;
  for (size_t i = 0; i < batchSize; ++i) {
    futures.emplace_back(folly::via(&executor, [] { doWork(); })
                             .via(&folly::InlineExecutor::instance()));
  }
  folly::collectAll(std::move(futures)).get();
}

folly::coro::Task<void> co_doWork() {
  co_await folly::coro::co_reschedule_on_current_executor;
  doWork();
}

void collectAllCoro(size_t batchSize) {
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    co_await folly::coro::collectAllRange(
        [&]() -> folly::coro::Generator<folly::coro::Task<void>&&> {
          for (size_t i = 0; i < batchSize; ++i) {
            co_yield co_doWork();
          }
        }())
        .scheduleOn(&executor);
  }());
}

void collectAllBaton(size_t batchSize) {
  folly::Baton<> baton;
  std::shared_ptr<folly::Baton<>> batonGuard(
      &baton, [](folly::Baton<>* baton) { baton->post(); });
  for (size_t i = 0; i < batchSize; ++i) {
    executor.add([batonGuard]() { doWork(); });
  }
  batonGuard.reset();
  baton.wait();
}

BENCHMARK(collectAllFuture10000, iters) {
  for (size_t i = 0; i < iters; ++i) {
    collectAllFuture(10000);
  }
}

BENCHMARK(collectAllFutureInline10000, iters) {
  for (size_t i = 0; i < iters; ++i) {
    collectAllFutureInline(10000);
  }
}

BENCHMARK(collectAllCoro10000, iters) {
  for (size_t i = 0; i < iters; ++i) {
    collectAllCoro(10000);
  }
}

BENCHMARK(collectAllBaton10000, iters) {
  for (size_t i = 0; i < iters; ++i) {
    collectAllBaton(10000);
  }
}

BENCHMARK(collectAllFuture100, iters) {
  for (size_t i = 0; i < iters; ++i) {
    collectAllFuture(100);
  }
}

BENCHMARK(collectAllFutureInline100, iters) {
  for (size_t i = 0; i < iters; ++i) {
    collectAllFutureInline(100);
  }
}

BENCHMARK(collectAllCoro100, iters) {
  for (size_t i = 0; i < iters; ++i) {
    collectAllCoro(100);
  }
}

BENCHMARK(collectAllBaton100, iters) {
  for (size_t i = 0; i < iters; ++i) {
    collectAllBaton(100);
  }
}

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
