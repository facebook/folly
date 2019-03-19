/*
 * Copyright 2019-present Facebook, Inc.
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

#include <folly/Portability.h>

#if FOLLY_HAS_COROUTINES

#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/executors/ManualExecutor.h>
#include <folly/experimental/coro/Baton.h>
#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/Collect.h>
#include <folly/experimental/coro/Mutex.h>
#include <folly/experimental/coro/Task.h>
#include <folly/portability/GTest.h>

#include <numeric>
#include <string>

////////////////////////////////////////////////////////
// folly::coro::collectAll() tests

TEST(CollectAll, WithNoArgs) {
  bool completed = false;
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    std::tuple<> result = co_await folly::coro::collectAll();
    completed = true;
    (void)result;
  }());
  CHECK(completed);
}

TEST(CollectAll, OneTaskWithValue) {
  folly::coro::Baton baton;
  auto f = [&]() -> folly::coro::Task<std::string> {
    co_await baton;
    co_return "hello";
  };

  bool completed = false;
  auto run = [&]() -> folly::coro::Task<void> {
    auto [result] = co_await folly::coro::collectAll(f());
    CHECK_EQ("hello", result);
    completed = true;
  };

  folly::ManualExecutor executor;

  auto future = run().scheduleOn(&executor).start();

  executor.drain();

  CHECK(!completed);

  baton.post();

  // Posting the baton should have just scheduled the 'f()' coroutine
  // for resumption on the executor but should not have executed
  // until we drain the executor again.
  CHECK(!completed);

  executor.drain();

  CHECK(completed);
  CHECK(future.isReady());
}

TEST(CollectAll, OneVoidTask) {
  bool completed = false;
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    // Checks that the task actually runs and that 'void' results are
    // promoted to folly::Unit when placed in a tuple.
    std::tuple<folly::Unit> result =
        co_await folly::coro::collectAll([&]() -> folly::coro::Task<void> {
          completed = true;
          co_return;
        }());
    (void)result;
  }());
  CHECK(completed);
}

TEST(CollectAll, CollectAllDoesntCompleteUntilAllTasksComplete) {
  folly::coro::Baton baton1;
  folly::coro::Baton baton2;
  bool task1Started = false;
  bool task2Started = false;
  bool complete = false;

  auto run = [&]() -> folly::coro::Task<void> {
    auto [first, second] = co_await folly::coro::collectAll(
        [&]() -> folly::coro::Task<void> {
          task1Started = true;
          co_await baton1;
        }(),
        [&]() -> folly::coro::Task<void> {
          task2Started = true;
          co_await baton2;
        }());
    complete = true;
    (void)first;
    (void)second;
  };

  folly::ManualExecutor executor;

  auto future = run().scheduleOn(&executor).start();

  CHECK(!task1Started);
  CHECK(!task2Started);

  executor.drain();

  CHECK(task1Started);
  CHECK(task2Started);
  CHECK(!complete);
  baton1.post();
  executor.drain();
  CHECK(!complete);
  baton2.post();
  executor.drain();
  CHECK(complete);
  CHECK(future.isReady());
}

struct ErrorA : std::exception {};
struct ErrorB : std::exception {};
struct ErrorC : std::exception {};

TEST(CollectAll, ThrowsOneOfMultipleErrors) {
  bool caughtException = false;
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    try {
      bool throwError = true;
      auto [x, y, z] = co_await folly::coro::collectAll(
          [&]() -> folly::coro::Task<int> {
            if (throwError) {
              throw ErrorA{};
            }
            co_return 1;
          }(),
          [&]() -> folly::coro::Task<int> {
            if (throwError) {
              throw ErrorB{};
            }
            co_return 2;
          }(),
          [&]() -> folly::coro::Task<int> {
            if (throwError) {
              throw ErrorC{};
            }
            co_return 3;
          }());
      (void)x;
      (void)y;
      (void)z;
      CHECK(false);
    } catch (const ErrorA&) {
      caughtException = true;
    } catch (const ErrorB&) {
      caughtException = true;
    } catch (const ErrorC&) {
      caughtException = true;
    }
  }());
  CHECK(caughtException);
}

TEST(CollectAll, SynchronousCompletionInLoopDoesntCauseStackOverflow) {
  // This test checks that collectAll() is using symmetric transfer to
  // resume the awaiting coroutine without consume stack-space.
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    for (int i = 0; i < 1'000'000; ++i) {
      auto [n, s] = co_await folly::coro::collectAll(
          []() -> folly::coro::Task<int> { co_return 123; }(),
          []() -> folly::coro::Task<std::string> { co_return "abc"; }());
      CHECK_EQ(n, 123);
      CHECK_EQ(s, "abc");
    }
  }());
}

template <
    typename Iter,
    typename Sentinel,
    typename BinaryOp,
    typename InitialValue = typename std::iterator_traits<Iter>::value_type>
folly::coro::Task<InitialValue> parallelAccumulate(
    Iter begin,
    Sentinel end,
    BinaryOp op,
    InitialValue initialValue = {}) {
  auto distance = std::distance(begin, end);
  if (distance < 512) {
    co_return std::accumulate(
        begin, end, std::move(initialValue), std::move(op));
  } else {
    auto mid = begin + (distance / 2);
    auto [first, second] = co_await folly::coro::collectAll(
        parallelAccumulate(begin, mid, op, std::move(initialValue))
            .scheduleOn(co_await folly::coro::co_current_executor),
        parallelAccumulate(mid + 1, end, op, *mid));
    co_return op(std::move(first), std::move(second));
  }
}

TEST(CollectAll, ParallelAccumulate) {
  folly::CPUThreadPoolExecutor threadPool{
      4, std::make_shared<folly::NamedThreadFactory>("TestThreadPool")};

  folly::coro::blockingWait(
      []() -> folly::coro::Task<void> {
        std::vector<int> values(100'000);
        for (int i = 0; i < 100'000; ++i) {
          values[i] = (1337 * i) % 1'000'000;
        }

        auto result = co_await parallelAccumulate(
            values.begin(), values.end(), [](int a, int b) {
              return std::max(a, b);
            });

        CHECK_EQ(999'989, result);
      }()
                  .scheduleOn(&threadPool));
}

/////////////////////////////////////////////////////////
// folly::coro::collectAllTry() tests

TEST(CollectAllTry, WithNoArgs) {
  bool completed = false;
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    std::tuple<> result = co_await folly::coro::collectAllTry();
    completed = true;
    (void)result;
  }());
  CHECK(completed);
}

TEST(CollectAllTry, OneTaskWithValue) {
  folly::coro::Baton baton;
  auto f = [&]() -> folly::coro::Task<std::string> {
    co_await baton;
    co_return "hello";
  };

  bool completed = false;
  auto run = [&]() -> folly::coro::Task<void> {
    auto [result] = co_await folly::coro::collectAllTry(f());
    CHECK_EQ("hello", result.value());
    completed = true;
  };

  folly::ManualExecutor executor;

  auto future = run().scheduleOn(&executor).start();

  executor.drain();

  CHECK(!completed);

  baton.post();

  // Posting the baton should have just scheduled the 'f()' coroutine
  // for resumption on the executor but should not have executed
  // until we drain the executor again.
  CHECK(!completed);

  executor.drain();

  CHECK(completed);
  CHECK(future.isReady());
}

TEST(CollectAllTry, OneTaskWithError) {
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    auto [result] =
        co_await folly::coro::collectAllTry([&]() -> folly::coro::Task<void> {
          if (false) {
            co_return;
          }
          throw ErrorA{};
        }());
    CHECK(!result.hasValue());
    CHECK(result.hasException());
    CHECK(result.exception().get_exception<ErrorA>() != nullptr);
  }());
}

TEST(CollectAllTry, PartialFailure) {
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    auto [aRes, bRes, cRes, dRes] = co_await folly::coro::collectAllTry(
        []() -> folly::coro::Task<int> { co_return 123; }(),
        []() -> folly::coro::Task<std::string> {
          if (true) {
            throw ErrorA{};
          }
          co_return "hello";
        }(),
        []() -> folly::coro::Task<void> {
          if (true) {
            throw ErrorB{};
          }
          co_return;
        }(),
        []() -> folly::coro::Task<double> { co_return 3.1415; }());
    CHECK(cRes.hasException());
    CHECK(cRes.exception().get_exception<ErrorB>() != nullptr);
    CHECK(dRes.hasValue());
    CHECK_EQ(3.1415, dRes.value());
  }());
}

#endif
