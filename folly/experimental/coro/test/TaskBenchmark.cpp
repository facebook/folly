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

#include <folly/Benchmark.h>
#include <folly/Portability.h>

#if FOLLY_HAS_COROUTINES

#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/CurrentExecutor.h>
#include <folly/experimental/coro/Task.h>

#include <memory>

BENCHMARK(SingleVoidSynchronousTaskInLoop, iters) {
  folly::coro::blockingWait([iters]() -> folly::coro::Task<void> {
    auto completeSynchronously = []() -> folly::coro::Task<void> { co_return; };
    for (std::size_t i = 0; i < iters; ++i) {
      co_await completeSynchronously();
    }
  }());
}

BENCHMARK(AsyncTaskInLoop, iters) {
  folly::coro::blockingWait([iters]() -> folly::coro::Task<void> {
    auto asyncReschedule = []() -> folly::coro::Task<void> {
      co_await folly::coro::co_reschedule_on_current_executor;
    };
    for (std::size_t i = 0; i < iters; ++i) {
      co_await asyncReschedule();
    }
  }());
}

BENCHMARK(AsyncRescheduleNoTask, iters) {
  folly::coro::blockingWait([iters]() -> folly::coro::Task<void> {
    for (std::size_t i = 0; i < iters; ++i) {
      co_await folly::coro::co_reschedule_on_current_executor;
    }
  }());
}

class Base {
 public:
  virtual ~Base() {}

  virtual folly::coro::Task<int> virtualMethod() = 0;
};

template <int Tag>
class Derived : public Base {
 public:
  FOLLY_NOINLINE
  folly::coro::Task<int> virtualMethod() override {
    co_return Tag;
  }
};

FOLLY_NOINLINE
static std::unique_ptr<Base> makeBase(size_t iters) {
  switch (iters % 3) {
    case 0:
      return std::make_unique<Derived<1>>();
    case 1:
      return std::make_unique<Derived<2>>();
    default:
      return std::make_unique<Derived<3>>();
  }
}

BENCHMARK(VirtualTaskMethod, iters) {
  folly::coro::blockingWait([iters]() -> folly::coro::Task<void> {
    auto base = makeBase(iters);
    size_t count = 0;
    for (std::size_t i = 0; i < iters; ++i) {
      count += co_await base->virtualMethod();
    }
    if (count != iters * ((iters % 3) + 1)) {
      std::terminate();
    }
  }());
}

template <size_t N>
static folly::coro::Task<void> staticNestedCalls() {
  if constexpr (N > 0) {
    co_await staticNestedCalls<N - 1>();
  }
  co_return;
}

template <size_t N>
static void benchStaticNestedCalls(size_t iters) {
  folly::coro::blockingWait([iters]() -> folly::coro::Task<void> {
    for (size_t i = 0; i < iters; ++i) {
      co_await staticNestedCalls<N>();
    }
  }());
}

BENCHMARK(StaticNestedCalls3, iters) {
  benchStaticNestedCalls<3>(iters / 3);
}

BENCHMARK(StaticNestedCalls10, iters) {
  benchStaticNestedCalls<10>(iters / 10);
}

static folly::coro::Task<void> nestedCalls(size_t depth) {
  if (depth > 0) {
    co_await nestedCalls(depth - 1);
  }
  co_return;
}

static void benchNestedCalls(size_t depth, size_t iters) {
  folly::coro::blockingWait([depth, iters]() -> folly::coro::Task<void> {
    for (size_t i = 0; i < iters; ++i) {
      co_await nestedCalls(depth);
    }
  }());
}

BENCHMARK(NestedCalls3, iters) {
  benchNestedCalls(3, iters / 3);
}

BENCHMARK(NestedCalls10, iters) {
  benchNestedCalls(10, iters / 10);
}

static void benchNestedCallsWithCancellation(size_t depth, size_t iters) {
  folly::CancellationSource cancelSource;
  folly::coro::blockingWait(folly::coro::co_withCancellation(
      cancelSource.getToken(), [depth, iters]() -> folly::coro::Task<void> {
        for (size_t i = 0; i < iters; ++i) {
          co_await nestedCalls(depth);
        }
      }()));
}

BENCHMARK(NestedCallsWithCancellation3, iters) {
  benchNestedCallsWithCancellation(3, iters / 3);
}

BENCHMARK(NestedCallsWithCancellation10, iters) {
  benchNestedCallsWithCancellation(10, iters / 10);
}

#endif // FOLLY_HAS_COROUTINES

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
