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

#include <folly/Portability.h>

#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/executors/ManualExecutor.h>
#include <folly/experimental/coro/Baton.h>
#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/Collect.h>
#include <folly/experimental/coro/CurrentExecutor.h>
#include <folly/experimental/coro/Generator.h>
#include <folly/experimental/coro/GtestHelpers.h>
#include <folly/experimental/coro/Mutex.h>
#include <folly/experimental/coro/Sleep.h>
#include <folly/experimental/coro/Task.h>
#include <folly/io/async/Request.h>
#include <folly/portability/GTest.h>

#include <numeric>
#include <string>
#include <vector>

#if FOLLY_HAS_COROUTINES

folly::coro::Task<void> sleepThatShouldBeCancelled(
    std::chrono::milliseconds dur) {
  EXPECT_THROW(co_await folly::coro::sleep(dur), folly::OperationCancelled);
}

class CollectAllTest : public testing::Test {};

TEST_F(CollectAllTest, WithNoArgs) {
  bool completed = false;
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    std::tuple<> result = co_await folly::coro::collectAll();
    completed = true;
    (void)result;
  }());
  EXPECT_TRUE(completed);
}

TEST_F(CollectAllTest, OneTaskWithValue) {
  folly::coro::Baton baton;
  auto f = [&]() -> folly::coro::Task<std::string> {
    co_await baton;
    co_return "hello";
  };

  bool completed = false;
  auto run = [&]() -> folly::coro::Task<void> {
    auto [result] = co_await folly::coro::collectAll(f());
    EXPECT_EQ("hello", result);
    completed = true;
  };

  folly::ManualExecutor executor;

  auto future = run().scheduleOn(&executor).start();

  executor.drain();

  EXPECT_FALSE(completed);

  baton.post();

  // Posting the baton should have just scheduled the 'f()' coroutine
  // for resumption on the executor but should not have executed
  // until we drain the executor again.
  EXPECT_FALSE(completed);

  executor.drain();

  EXPECT_TRUE(completed);
  EXPECT_TRUE(future.isReady());
}

TEST_F(CollectAllTest, OneVoidTask) {
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
  EXPECT_TRUE(completed);
}

TEST_F(CollectAllTest, CollectAllDoesntCompleteUntilAllTasksComplete) {
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

  EXPECT_FALSE(task1Started);
  EXPECT_FALSE(task2Started);

  executor.drain();

  EXPECT_TRUE(task1Started);
  EXPECT_TRUE(task2Started);
  EXPECT_FALSE(complete);
  baton1.post();
  executor.drain();
  EXPECT_FALSE(complete);
  baton2.post();
  executor.drain();
  EXPECT_TRUE(complete);
  EXPECT_TRUE(future.isReady());
}

struct ErrorA : std::exception {};
struct ErrorB : std::exception {};
struct ErrorC : std::exception {};

TEST_F(CollectAllTest, ThrowsFirstError) {
  bool caughtException = false;
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    try {
      bool throwError = true;
      // Child tasks are started in-order.
      // The first task will reschedule itself onto the executor.
      // The second task will fail immediately and will be the first
      // task to fail.
      // Then the third and first tasks will fail.
      // As the second task failed first we should see its exception
      // propagate out of collectAll().
      auto [x, y, z] = co_await folly::coro::collectAll(
          [&]() -> folly::coro::Task<int> {
            co_await folly::coro::co_reschedule_on_current_executor;
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
      ADD_FAILURE() << "Hit unexpected codepath";
    } catch (const ErrorB&) {
      caughtException = true;
    }
  }());
  EXPECT_TRUE(caughtException);
}

TEST_F(CollectAllTest, SynchronousCompletionInLoopDoesntCauseStackOverflow) {
  // This test checks that collectAll() is using symmetric transfer to
  // resume the awaiting coroutine without consume stack-space.
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    for (int i = 0; i < 1'000'000; ++i) {
      auto [n, s] = co_await folly::coro::collectAll(
          []() -> folly::coro::Task<int> { co_return 123; }(),
          []() -> folly::coro::Task<std::string> { co_return "abc"; }());
      EXPECT_EQ(n, 123);
      EXPECT_EQ(s, "abc");
    }
  }());
}

struct OperationCancelled : std::exception {};

template <
    typename Iter,
    typename Sentinel,
    typename BinaryOp,
    typename InitialValue = typename std::iterator_traits<Iter>::value_type>
folly::coro::Task<InitialValue> parallelAccumulate(
    Iter begin, Sentinel end, BinaryOp op, InitialValue initialValue = {}) {
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

TEST_F(CollectAllTest, ParallelAccumulate) {
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

        EXPECT_EQ(999'989, result);
      }()
                  .scheduleOn(&threadPool));
}

TEST_F(CollectAllTest, CollectAllCancelsSubtasksWhenASubtaskFails) {
  using namespace std::chrono_literals;

  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    auto start = std::chrono::steady_clock::now();
    try {
      auto [a, b, c] = co_await folly::coro::collectAll(
          []() -> folly::coro::Task<int> {
            co_await folly::coro::sleep(10s);
            co_return 42;
          }(),
          []() -> folly::coro::Task<float> {
            co_await folly::coro::sleep(5s);
            co_return 3.14f;
          }(),
          []() -> folly::coro::Task<void> {
            co_await folly::coro::co_reschedule_on_current_executor;
            throw ErrorA{};
          }());
      ADD_FAILURE() << "Hit unexpected codepath";
      (void)a;
      (void)b;
      (void)c;
    } catch (const ErrorA&) {
    }
    auto end = std::chrono::steady_clock::now();

    EXPECT_LT(end - start, 1s);
  }());
}

TEST_F(CollectAllTest, CollectAllCancelsSubtasksWhenParentTaskCancelled) {
  using namespace std::chrono_literals;

  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    auto start = std::chrono::steady_clock::now();
    folly::CancellationSource cancelSource;
    auto [a, b, c] = co_await folly::coro::co_withCancellation(
        cancelSource.getToken(),
        folly::coro::collectAll(
            [&]() -> folly::coro::Task<int> {
              co_await sleepThatShouldBeCancelled(10s);
              co_return 42;
            }(),
            [&]() -> folly::coro::Task<float> {
              co_await sleepThatShouldBeCancelled(5s);
              co_return 3.14f;
            }(),
            [&]() -> folly::coro::Task<void> {
              co_await folly::coro::co_reschedule_on_current_executor;
              co_await folly::coro::co_reschedule_on_current_executor;
              co_await folly::coro::co_reschedule_on_current_executor;
              cancelSource.requestCancellation();
            }()));
    auto end = std::chrono::steady_clock::now();

    EXPECT_LT(end - start, 1s);
    EXPECT_EQ(42, a);
    EXPECT_EQ(3.14f, b);
    (void)c;
  }());
}

TEST_F(CollectAllTest, CancellationTokenRemainsActiveAfterReturn) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    folly::CancellationSource cancelSource;
    folly::coro::AsyncScope scope;
    folly::coro::Baton baton;
    auto task = folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
      auto token = co_await folly::coro::co_current_cancellation_token;
      auto ex = co_await folly::coro::co_current_executor;
      scope.add(
          folly::coro::co_withCancellation(
              token, folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
                auto innerToken =
                    co_await folly::coro::co_current_cancellation_token;
                co_await baton;
                EXPECT_TRUE(innerToken.isCancellationRequested());
              }))
              .scheduleOn(ex));
    });

    co_await folly::coro::co_withCancellation(
        cancelSource.getToken(), folly::coro::collectAll(std::move(task)));
    cancelSource.requestCancellation();
    baton.post();
    co_await scope.joinAsync();
  }());
}

namespace {

class TestRequestData : public folly::RequestData {
 public:
  explicit TestRequestData() noexcept {}

  bool hasCallback() override { return false; }
};

} // namespace

TEST_F(CollectAllTest, CollectAllKeepsRequestContextOfChildTasksIndependent) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    folly::RequestContextScopeGuard requestScope;

    auto getContextData = []() {
      return folly::RequestContext::get()->getContextData("test");
    };

    auto setContextData = []() {
      folly::RequestContext::get()->setContextData(
          "test", std::make_unique<TestRequestData>());
    };

    setContextData();
    auto initialContextData = getContextData();

    auto makeChildTask = [&]() -> folly::coro::Task<void> {
      EXPECT_TRUE(getContextData() == initialContextData);
      folly::RequestContextScopeGuard childScope;
      EXPECT_TRUE(getContextData() == nullptr);
      co_await folly::coro::co_reschedule_on_current_executor;
      EXPECT_TRUE(getContextData() == nullptr);
      setContextData();
      auto newContextData = getContextData();
      EXPECT_TRUE(newContextData != nullptr);
      EXPECT_TRUE(newContextData != initialContextData);
      co_await folly::coro::co_reschedule_on_current_executor;
      EXPECT_TRUE(getContextData() == newContextData);
    };

    co_await folly::coro::collectAll(makeChildTask(), makeChildTask());

    EXPECT_TRUE(getContextData() == initialContextData);
  }());
}

TEST_F(CollectAllTest, TaskWithExecutorUsage) {
  folly::CPUThreadPoolExecutor threadPool{
      4, std::make_shared<folly::NamedThreadFactory>("TestThreadPool")};

  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    auto [a, b] = co_await folly::coro::collectAll(
        []() -> folly::coro::Task<int> { co_return 42; }().scheduleOn(
                 &threadPool),
        []() -> folly::coro::Task<std::string> { co_return "coroutine"; }()
                    .scheduleOn(&threadPool));
    EXPECT_TRUE(a == 42);
    EXPECT_TRUE(b == "coroutine");
  }());
}

class CollectAllTryTest : public testing::Test {};

TEST_F(CollectAllTryTest, WithNoArgs) {
  bool completed = false;
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    std::tuple<> result = co_await folly::coro::collectAllTry();
    completed = true;
    (void)result;
  }());
  EXPECT_TRUE(completed);
}

TEST_F(CollectAllTryTest, OneTaskWithValue) {
  folly::coro::Baton baton;
  auto f = [&]() -> folly::coro::Task<std::string> {
    co_await baton;
    co_return "hello";
  };

  bool completed = false;
  auto run = [&]() -> folly::coro::Task<void> {
    auto [result] = co_await folly::coro::collectAllTry(f());
    EXPECT_EQ("hello", result.value());
    completed = true;
  };

  folly::ManualExecutor executor;

  auto future = run().scheduleOn(&executor).start();

  executor.drain();

  EXPECT_FALSE(completed);

  baton.post();

  // Posting the baton should have just scheduled the 'f()' coroutine
  // for resumption on the executor but should not have executed
  // until we drain the executor again.
  EXPECT_FALSE(completed);

  executor.drain();

  EXPECT_TRUE(completed);
  EXPECT_TRUE(future.isReady());
}

TEST_F(CollectAllTryTest, OneTaskWithError) {
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    auto [result] =
        co_await folly::coro::collectAllTry([&]() -> folly::coro::Task<void> {
          if (false) {
            co_return;
          }
          throw ErrorA{};
        }());
    EXPECT_FALSE(result.hasValue());
    EXPECT_TRUE(result.hasException());
    EXPECT_TRUE(result.exception().get_exception<ErrorA>() != nullptr);
  }());
}

TEST_F(CollectAllTryTest, PartialFailure) {
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
    EXPECT_TRUE(cRes.hasException());
    EXPECT_TRUE(cRes.exception().get_exception<ErrorB>() != nullptr);
    EXPECT_TRUE(dRes.hasValue());
    EXPECT_EQ(3.1415, dRes.value());
  }());
}

TEST_F(CollectAllTryTest, CollectAllTryDoesNotCancelSubtasksWhenASubtaskFails) {
  using namespace std::chrono_literals;

  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    auto [a, b, c] = co_await folly::coro::collectAllTry(
        []() -> folly::coro::Task<int> {
          co_await folly::coro::co_reschedule_on_current_executor;
          co_await folly::coro::co_reschedule_on_current_executor;
          co_await folly::coro::co_reschedule_on_current_executor;
          co_await folly::coro::co_reschedule_on_current_executor;
          co_await folly::coro::co_reschedule_on_current_executor;
          EXPECT_FALSE((co_await folly::coro::co_current_cancellation_token)
                           .isCancellationRequested());
          co_return 42;
        }(),
        []() -> folly::coro::Task<float> {
          co_await folly::coro::co_reschedule_on_current_executor;
          co_await folly::coro::co_reschedule_on_current_executor;
          co_await folly::coro::co_reschedule_on_current_executor;
          co_await folly::coro::co_reschedule_on_current_executor;
          co_await folly::coro::co_reschedule_on_current_executor;
          EXPECT_FALSE((co_await folly::coro::co_current_cancellation_token)
                           .isCancellationRequested());
          co_return 3.14f;
        }(),
        []() -> folly::coro::Task<void> {
          co_await folly::coro::co_reschedule_on_current_executor;
          throw ErrorA{};
        }());

    EXPECT_TRUE(a.hasValue());
    EXPECT_EQ(42, a.value());
    EXPECT_TRUE(b.hasValue());
    EXPECT_EQ(3.14f, b.value());
    EXPECT_TRUE(c.hasException());
  }());
}

TEST_F(CollectAllTryTest, CollectAllCancelsSubtasksWhenParentTaskCancelled) {
  using namespace std::chrono_literals;

  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    auto start = std::chrono::steady_clock::now();
    folly::CancellationSource cancelSource;
    auto [a, b, c] = co_await folly::coro::co_withCancellation(
        cancelSource.getToken(),
        folly::coro::collectAllTry(
            [&]() -> folly::coro::Task<int> {
              co_await sleepThatShouldBeCancelled(10s);
              co_return 42;
            }(),
            [&]() -> folly::coro::Task<float> {
              co_await sleepThatShouldBeCancelled(5s);
              co_return 3.14f;
            }(),
            [&]() -> folly::coro::Task<void> {
              co_await folly::coro::co_reschedule_on_current_executor;
              co_await folly::coro::co_reschedule_on_current_executor;
              co_await folly::coro::co_reschedule_on_current_executor;
              cancelSource.requestCancellation();
            }()));
    auto end = std::chrono::steady_clock::now();

    EXPECT_LT(end - start, 1s);
    EXPECT_EQ(42, a.value());
    EXPECT_EQ(3.14f, b.value());
    EXPECT_TRUE(c.hasValue());
  }());
}

TEST_F(CollectAllTryTest, CancellationTokenRemainsActiveAfterReturn) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    folly::CancellationSource cancelSource;
    folly::coro::AsyncScope scope;
    folly::coro::Baton baton;
    auto task = folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
      auto token = co_await folly::coro::co_current_cancellation_token;
      auto ex = co_await folly::coro::co_current_executor;
      scope.add(
          folly::coro::co_withCancellation(
              token, folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
                auto innerToken =
                    co_await folly::coro::co_current_cancellation_token;
                co_await baton;
                EXPECT_TRUE(innerToken.isCancellationRequested());
              }))
              .scheduleOn(ex));
    });

    co_await folly::coro::co_withCancellation(
        cancelSource.getToken(), folly::coro::collectAllTry(std::move(task)));
    cancelSource.requestCancellation();
    baton.post();
    co_await scope.joinAsync();
  }());
}

TEST_F(CollectAllTryTest, KeepsRequestContextOfChildTasksIndependent) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    folly::RequestContextScopeGuard requestScope;

    auto getContextData = []() {
      return folly::RequestContext::get()->getContextData("test");
    };

    auto setContextData = []() {
      folly::RequestContext::get()->setContextData(
          "test", std::make_unique<TestRequestData>());
    };

    setContextData();
    auto initialContextData = getContextData();

    auto makeChildTask = [&]() -> folly::coro::Task<void> {
      EXPECT_TRUE(getContextData() == initialContextData);
      folly::RequestContextScopeGuard childScope;
      EXPECT_TRUE(getContextData() == nullptr);
      co_await folly::coro::co_reschedule_on_current_executor;
      EXPECT_TRUE(getContextData() == nullptr);
      setContextData();
      auto newContextData = getContextData();
      EXPECT_TRUE(newContextData != nullptr);
      EXPECT_TRUE(newContextData != initialContextData);
      co_await folly::coro::co_reschedule_on_current_executor;
      EXPECT_TRUE(getContextData() == newContextData);
    };

    co_await folly::coro::collectAllTry(makeChildTask(), makeChildTask());

    EXPECT_TRUE(getContextData() == initialContextData);
  }());
}

class CollectAllRangeTest : public testing::Test {};

TEST_F(CollectAllRangeTest, EmptyRangeOfVoidTask) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    std::vector<folly::coro::Task<void>> tasks;
    auto collectTask = folly::coro::collectAllRange(std::move(tasks));
    static_assert(
        std::is_void<
            folly::coro::semi_await_result_t<decltype(collectTask)>>::value,
        "Result of awaiting collectAllRange() of Task<void> should be void");
    co_await std::move(collectTask);
  }());
}

TEST_F(CollectAllRangeTest, RangeOfVoidAllSucceeding) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    int count = 0;
    auto makeTask = [&]() -> folly::coro::Task<void> {
      ++count;
      co_return;
    };

    std::vector<folly::coro::Task<void>> tasks;
    tasks.push_back(makeTask());
    tasks.push_back(makeTask());
    tasks.push_back(makeTask());

    co_await folly::coro::collectAllRange(std::move(tasks));

    EXPECT_EQ(3, count);
  }());
}

TEST_F(CollectAllRangeTest, RangeOfVoidSomeFailing) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    int count = 0;
    auto makeTask = [&]() -> folly::coro::Task<void> {
      if ((++count % 3) == 0) {
        throw ErrorA{};
      }
      co_return;
    };

    std::vector<folly::coro::Task<void>> tasks;
    tasks.push_back(makeTask());
    tasks.push_back(makeTask());
    tasks.push_back(makeTask());
    tasks.push_back(makeTask());
    tasks.push_back(makeTask());

    try {
      co_await folly::coro::collectAllRange(std::move(tasks));
      ADD_FAILURE() << "Hit unexpected codepath";
    } catch (const ErrorA&) {
    }

    EXPECT_EQ(5, count);
  }());
}

TEST_F(CollectAllRangeTest, RangeOfNonVoid) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    int count = 0;
    auto makeTask = [&]() -> folly::coro::Task<int> {
      using namespace std::literals::chrono_literals;
      int x = count++;
      if ((x % 20) == 0) {
        co_await folly::coro::co_reschedule_on_current_executor;
      }
      co_return x;
    };

    constexpr int taskCount = 50;

    std::vector<folly::coro::Task<int>> tasks;
    for (int i = 0; i < taskCount; ++i) {
      tasks.push_back(makeTask());
    }

    EXPECT_EQ(0, count);

    std::vector<int> results =
        co_await folly::coro::collectAllRange(std::move(tasks));

    EXPECT_EQ(taskCount, results.size());
    EXPECT_EQ(taskCount, count);

    for (int i = 0; i < taskCount; ++i) {
      EXPECT_EQ(i, results[i]);
    }
  }());
}

TEST_F(CollectAllRangeTest, SubtasksCancelledWhenASubtaskFails) {
  using namespace std::chrono_literals;

  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    bool consumedAllTasks = false;
    auto generateTasks =
        [&]() -> folly::coro::Generator<folly::coro::Task<void>&&> {
      for (int i = 0; i < 10; ++i) {
        co_yield folly::coro::sleep(10s);
      }

      co_yield []() -> folly::coro::Task<void> {
        co_await folly::coro::co_reschedule_on_current_executor;
        throw ErrorA{};
      }();

      for (int i = 0; i < 10; ++i) {
        co_yield folly::coro::sleep(10s);
      }

      consumedAllTasks = true;
    };

    auto start = std::chrono::steady_clock::now();
    try {
      co_await folly::coro::collectAllRange(generateTasks());
      ADD_FAILURE() << "Hit unexpected codepath";
    } catch (const ErrorA&) {
    }
    auto end = std::chrono::steady_clock::now();

    EXPECT_LT(end - start, 1s);
    EXPECT_TRUE(consumedAllTasks);
  }());
}

TEST_F(CollectAllRangeTest, FailsWithErrorOfFirstTaskToFailWhenMultipleErrors) {
  using namespace std::chrono_literals;

  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    try {
      co_await folly::coro::collectAllRange(
          []() -> folly::coro::Generator<folly::coro::Task<void>&&> {
            co_yield folly::coro::sleep(1s);
            co_yield []() -> folly::coro::Task<> {
              co_await folly::coro::sleep(1s);
              throw ErrorA{};
            }();
            co_yield []() -> folly::coro::Task<> {
              co_await folly::coro::co_reschedule_on_current_executor;
              throw ErrorB{};
            }();
            co_yield folly::coro::sleep(2s);
          }());
      ADD_FAILURE() << "Hit unexpected codepath";
    } catch (const ErrorB&) {
    }
  }());
}

TEST_F(CollectAllRangeTest, SubtasksCancelledWhenParentTaskCancelled) {
  using namespace std::chrono_literals;

  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    folly::CancellationSource cancelSource;

    bool consumedAllTasks = false;
    auto generateTasks =
        [&]() -> folly::coro::Generator<folly::coro::Task<void>&&> {
      for (int i = 0; i < 10; ++i) {
        co_yield folly::coro::sleepReturnEarlyOnCancel(10s);
      }

      co_yield [&]() -> folly::coro::Task<void> {
        co_await folly::coro::co_reschedule_on_current_executor;
        co_await folly::coro::co_reschedule_on_current_executor;
        co_await folly::coro::co_reschedule_on_current_executor;
        co_await folly::coro::co_reschedule_on_current_executor;
        cancelSource.requestCancellation();

        auto token = co_await folly::coro::co_current_cancellation_token;
        EXPECT_TRUE(token.isCancellationRequested());
      }();

      consumedAllTasks = true;
    };

    auto start = std::chrono::steady_clock::now();
    co_await folly::coro::co_withCancellation(
        cancelSource.getToken(), folly::coro::collectAllRange(generateTasks()));
    auto end = std::chrono::steady_clock::now();

    EXPECT_LT(end - start, 1s);
    EXPECT_TRUE(consumedAllTasks);
  }());
}

TEST_F(CollectAllRangeTest, CancellationTokenRemainsActiveAfterReturn) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    folly::CancellationSource cancelSource;
    folly::coro::AsyncScope scope;
    folly::coro::Baton baton;
    auto generateTasks =
        [&]() -> folly::coro::Generator<folly::coro::Task<void>&&> {
      co_yield folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
        auto token = co_await folly::coro::co_current_cancellation_token;
        auto ex = co_await folly::coro::co_current_executor;
        scope.add(
            folly::coro::co_withCancellation(
                token, folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
                  auto innerToken =
                      co_await folly::coro::co_current_cancellation_token;
                  co_await baton;
                  EXPECT_TRUE(innerToken.isCancellationRequested());
                }))
                .scheduleOn(ex));
      });
    };

    co_await folly::coro::co_withCancellation(
        cancelSource.getToken(), folly::coro::collectAllRange(generateTasks()));
    cancelSource.requestCancellation();
    baton.post();
    co_await scope.joinAsync();
  }());
}

TEST_F(CollectAllRangeTest, KeepsRequestContextOfChildTasksIndependent) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    folly::RequestContextScopeGuard requestScope;

    auto getContextData = []() {
      return folly::RequestContext::get()->getContextData("test");
    };

    auto setContextData = []() {
      folly::RequestContext::get()->setContextData(
          "test", std::make_unique<TestRequestData>());
    };

    setContextData();
    auto initialContextData = getContextData();

    auto makeChildTask = [&]() -> folly::coro::Task<void> {
      EXPECT_TRUE(getContextData() == initialContextData);
      folly::RequestContextScopeGuard childScope;
      EXPECT_TRUE(getContextData() == nullptr);
      co_await folly::coro::co_reschedule_on_current_executor;
      EXPECT_TRUE(getContextData() == nullptr);
      setContextData();
      auto newContextData = getContextData();
      EXPECT_TRUE(newContextData != nullptr);
      EXPECT_TRUE(newContextData != initialContextData);
      co_await folly::coro::co_reschedule_on_current_executor;
      EXPECT_TRUE(getContextData() == newContextData);
    };

    std::vector<folly::coro::Task<void>> tasks;
    tasks.emplace_back(makeChildTask());
    tasks.emplace_back(makeChildTask());
    tasks.emplace_back(makeChildTask());

    co_await folly::coro::collectAllRange(std::move(tasks));

    EXPECT_TRUE(getContextData() == initialContextData);
  }());
}

TEST_F(CollectAllRangeTest, VectorOfTaskWithExecutorUsage) {
  folly::CPUThreadPoolExecutor threadPool{
      4, std::make_shared<folly::NamedThreadFactory>("TestThreadPool")};

  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    std::vector<folly::coro::TaskWithExecutor<int>> tasks;
    for (int i = 0; i < 4; ++i) {
      tasks.push_back(
          [](int idx) -> folly::coro::Task<int> { co_return idx + 1; }(i)
                             .scheduleOn(&threadPool));
    }

    auto results = co_await folly::coro::collectAllRange(std::move(tasks));
    EXPECT_TRUE(results.size() == 4);
    EXPECT_TRUE(results[0] == 1);
    EXPECT_TRUE(results[1] == 2);
    EXPECT_TRUE(results[2] == 3);
    EXPECT_TRUE(results[3] == 4);
  }());
}

TEST_F(CollectAllRangeTest, GeneratorFromRange) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    folly::coro::CancellableAsyncScope scope;
    auto makeTask = [](int i) -> folly::coro::Task<int> {
      co_await folly::coro::sleep(std::chrono::milliseconds(100 * i));
      co_return i;
    };
    std::vector<folly::coro::Task<int>> tasks;
    for (int i = 5; i > 0; --i) {
      tasks.push_back(makeTask(i));
    }

    auto results =
        folly::coro::makeUnorderedAsyncGenerator(scope, std::move(tasks));
    // co_await doesn't work inside EXPECT_EQ
    EXPECT_TRUE(*(co_await results.next()) == 1);
    EXPECT_TRUE(*(co_await results.next()) == 2);
    EXPECT_TRUE(*(co_await results.next()) == 3);
    EXPECT_TRUE(*(co_await results.next()) == 4);
    EXPECT_TRUE(*(co_await results.next()) == 5);
    EXPECT_FALSE(co_await results.next());
    co_await scope.joinAsync();
  }());
}

CO_TEST_F(CollectAllRangeTest, GeneratorFromVoidRange) {
  folly::coro::CancellableAsyncScope scope;
  auto makeTask = [](int i) -> folly::coro::Task<void> {
    co_await folly::coro::sleep(std::chrono::milliseconds(100 * i));
    if (i == 4) {
      throw std::runtime_error("fail on 4");
    }
    co_return;
  };
  std::vector<folly::coro::Task<void>> tasks;
  for (int i = 5; i > 0; --i) {
    tasks.push_back(makeTask(i));
  }

  auto results =
      folly::coro::makeUnorderedAsyncGenerator(scope, std::move(tasks));
  // The first 3 results should be produced normally
  co_await results.next();
  co_await results.next();
  co_await results.next();
  // The next should generate an exception
  try {
    co_await results.next();
    ADD_FAILURE() << "expected an exception";
  } catch (const std::runtime_error& ex) {
    EXPECT_STREQ(ex.what(), "fail on 4");
  }
  co_await scope.joinAsync();
}

TEST_F(CollectAllRangeTest, GeneratorFromRangePartialConsume) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    folly::coro::AsyncScope scope;
    auto makeTask = [](int i) -> folly::coro::Task<int> { co_return i; };
    auto generateTasks =
        [&]() -> folly::coro::Generator<folly::coro::Task<int>&&> {
      for (int i = 5; i > 0; --i) {
        co_yield makeTask(i);
      }
    };

    auto results =
        folly::coro::makeUnorderedAsyncGenerator(scope, generateTasks());
    for (int i = 0; i < 3; ++i) {
      co_await results.next();
    }
    co_await scope.joinAsync();
  }());
}

TEST_F(CollectAllRangeTest, GeneratorFromRangeFailed) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    folly::coro::AsyncScope scope;
    auto makeTask = [](int i) -> folly::coro::Task<int> {
      co_await folly::coro::sleep(std::chrono::milliseconds(100 * i));
      co_return i;
    };
    auto generateTasks =
        [&]() -> folly::coro::Generator<folly::coro::Task<int>&&> {
      co_yield []() -> folly::coro::Task<int> {
        co_await folly::coro::sleep(std::chrono::milliseconds(350));
        co_yield folly::coro::co_error(std::runtime_error("foo"));
      }();
      for (int i = 5; i > 0; --i) {
        co_yield makeTask(i);
      }
    };

    auto results =
        folly::coro::makeUnorderedAsyncGenerator(scope, generateTasks());
    // co_await doesn't work inside EXPECT_EQ
    EXPECT_TRUE(*(co_await results.next()) == 1);
    EXPECT_TRUE(*(co_await results.next()) == 2);
    EXPECT_TRUE(*(co_await results.next()) == 3);
    EXPECT_TRUE((co_await co_awaitTry(results.next()))
                    .hasException<std::runtime_error>());
    co_await scope.joinAsync();
  }());
}

TEST_F(CollectAllRangeTest, GeneratorFromRangeCancelled) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    folly::coro::AsyncScope scope;
    auto makeTask = [](int i) -> folly::coro::Task<int> {
      co_await folly::coro::sleep(std::chrono::milliseconds(1000 * i));
      co_return i;
    };
    folly::CancellationSource cancelSource;
    auto generateTasks =
        [&]() -> folly::coro::Generator<folly::coro::Task<int>&&> {
      for (int i = 0; i < 10; ++i) {
        co_yield makeTask(i);
        if (i == 4) {
          cancelSource.requestCancellation();
        }
      }
    };
    auto start = std::chrono::steady_clock::now();
    auto results =
        folly::coro::makeUnorderedAsyncGenerator(scope, generateTasks());
    auto result = co_await folly::coro::co_withCancellation(
        cancelSource.getToken(), co_awaitTry(results.next()));
    auto end = std::chrono::steady_clock::now();

    EXPECT_LT(end - start, std::chrono::milliseconds(1000));
    EXPECT_TRUE(result.hasException<folly::OperationCancelled>());
    co_await scope.joinAsync();
  }());
}

TEST_F(CollectAllRangeTest, GeneratorFromRangeCancelledFromScope) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    folly::coro::CancellableAsyncScope scope;
    auto makeTask = [](int i) -> folly::coro::Task<int> {
      co_await folly::coro::sleep(std::chrono::milliseconds(1000 * i));
      co_return i;
    };
    folly::CancellationSource cancelSource;
    auto generateTasks =
        [&]() -> folly::coro::Generator<folly::coro::Task<int>&&> {
      for (int i = 0; i < 10; ++i) {
        co_yield makeTask(i);
        if (i == 4) {
          scope.requestCancellation();
        }
      }
    };
    auto start = std::chrono::steady_clock::now();
    auto results =
        folly::coro::makeUnorderedAsyncGenerator(scope, generateTasks());
    auto result = co_await folly::coro::co_withCancellation(
        cancelSource.getToken(), co_awaitTry(results.next()));
    auto end = std::chrono::steady_clock::now();

    EXPECT_LT(end - start, std::chrono::milliseconds(1000));
    EXPECT_TRUE(result.hasException<folly::OperationCancelled>());
    co_await scope.joinAsync();
  }());
}

TEST_F(CollectAllRangeTest, GeneratorFromEmptyRange) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    folly::coro::AsyncScope scope;
    std::vector<folly::coro::Task<void>> tasks;
    auto results =
        folly::coro::makeUnorderedAsyncGenerator(scope, std::move(tasks));
    while (auto next = co_await results.next()) {
      EXPECT_FALSE(true) << "Unexpected result";
    }
    co_await scope.joinAsync();
  }());
}

class CollectAllTryRangeTest : public testing::Test {};

TEST_F(CollectAllTryRangeTest, RangeOfVoidSomeFailing) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    int count = 0;
    auto makeTask = [&]() -> folly::coro::Task<void> {
      if ((++count % 3) == 0) {
        throw ErrorA{};
      }
      co_return;
    };

    std::vector<folly::coro::Task<void>> tasks;
    tasks.push_back(makeTask());
    tasks.push_back(makeTask());
    tasks.push_back(makeTask());
    tasks.push_back(makeTask());
    tasks.push_back(makeTask());

    auto results = co_await folly::coro::collectAllTryRange(std::move(tasks));

    EXPECT_EQ(5, results.size());
    EXPECT_TRUE(results[0].hasValue());
    EXPECT_TRUE(results[1].hasValue());
    EXPECT_TRUE(results[2].hasException());
    EXPECT_TRUE(results[3].hasValue());
    EXPECT_TRUE(results[4].hasValue());
  }());
}

TEST_F(CollectAllTryRangeTest, RangeOfValueSomeFailing) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    int count = 0;
    auto makeTask = [&]() -> folly::coro::Task<std::string> {
      if ((++count % 3) == 0) {
        throw ErrorA{};
      }
      co_return "testing";
    };

    std::vector<folly::coro::Task<std::string>> tasks;
    tasks.push_back(makeTask());
    tasks.push_back(makeTask());
    tasks.push_back(makeTask());
    tasks.push_back(makeTask());
    tasks.push_back(makeTask());
    tasks.push_back(makeTask());

    auto results = co_await folly::coro::collectAllTryRange(std::move(tasks));

    EXPECT_EQ(6, results.size());
    EXPECT_TRUE(results[0].hasValue());
    EXPECT_EQ("testing", results[0].value());
    EXPECT_TRUE(results[1].hasValue());
    EXPECT_EQ("testing", results[1].value());
    EXPECT_TRUE(results[2].hasException());
    EXPECT_TRUE(results[3].hasValue());
    EXPECT_EQ("testing", results[3].value());
    EXPECT_TRUE(results[4].hasValue());
    EXPECT_EQ("testing", results[4].value());
    EXPECT_TRUE(results[5].hasException());
  }());
}

TEST_F(CollectAllTryRangeTest, NotCancelledWhenSubtaskFails) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    auto generateTasks =
        [&]() -> folly::coro::Generator<folly::coro::Task<void>&&> {
      auto makeValidationTask = []() -> folly::coro::Task<void> {
        co_await folly::coro::co_reschedule_on_current_executor;
        co_await folly::coro::co_reschedule_on_current_executor;
        co_await folly::coro::co_reschedule_on_current_executor;
        auto token = co_await folly::coro::co_current_cancellation_token;
        EXPECT_FALSE(token.isCancellationRequested());
      };

      co_yield makeValidationTask();
      co_yield makeValidationTask();

      co_yield []() -> folly::coro::Task<void> {
        co_await folly::coro::co_reschedule_on_current_executor;
        throw ErrorA{};
      }();

      co_yield makeValidationTask();
      co_yield makeValidationTask();
    };

    auto results = co_await folly::coro::collectAllTryRange(generateTasks());
    EXPECT_EQ(5, results.size());
    EXPECT_TRUE(results[0].hasValue());
    EXPECT_TRUE(results[1].hasValue());
    EXPECT_TRUE(results[2].hasException());
    EXPECT_TRUE(results[3].hasValue());
    EXPECT_TRUE(results[4].hasValue());
  }());
}

TEST_F(CollectAllTryRangeTest, SubtasksCancelledWhenParentTaskCancelled) {
  using namespace std::chrono_literals;

  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    folly::CancellationSource cancelSource;

    bool consumedAllTasks = false;
    auto generateTasks =
        [&]() -> folly::coro::Generator<folly::coro::Task<void>&&> {
      for (int i = 0; i < 10; ++i) {
        co_yield sleepThatShouldBeCancelled(10s);
      }

      co_yield [&]() -> folly::coro::Task<void> {
        co_await folly::coro::co_reschedule_on_current_executor;
        co_await folly::coro::co_reschedule_on_current_executor;
        co_await folly::coro::co_reschedule_on_current_executor;
        co_await folly::coro::co_reschedule_on_current_executor;
        cancelSource.requestCancellation();

        auto token = co_await folly::coro::co_current_cancellation_token;
        EXPECT_TRUE(token.isCancellationRequested());
      }();

      consumedAllTasks = true;
    };

    auto start = std::chrono::steady_clock::now();
    auto results = co_await folly::coro::co_withCancellation(
        cancelSource.getToken(),
        folly::coro::collectAllTryRange(generateTasks()));
    auto end = std::chrono::steady_clock::now();

    EXPECT_EQ(11, results.size());
    for (auto& result : results) {
      EXPECT_TRUE(result.hasValue());
    }
    EXPECT_LT(end - start, 1s);
    EXPECT_TRUE(consumedAllTasks);
  }());
}

TEST_F(CollectAllTryRangeTest, CancellationTokenRemainsActiveAfterReturn) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    folly::CancellationSource cancelSource;
    folly::coro::AsyncScope scope;
    folly::coro::Baton baton;
    auto generateTasks =
        [&]() -> folly::coro::Generator<folly::coro::Task<void>&&> {
      co_yield folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
        auto token = co_await folly::coro::co_current_cancellation_token;
        auto ex = co_await folly::coro::co_current_executor;
        scope.add(
            folly::coro::co_withCancellation(
                token, folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
                  auto innerToken =
                      co_await folly::coro::co_current_cancellation_token;
                  co_await baton;
                  EXPECT_TRUE(innerToken.isCancellationRequested());
                }))
                .scheduleOn(ex));
      });
    };

    co_await folly::coro::co_withCancellation(
        cancelSource.getToken(),
        folly::coro::collectAllTryRange(generateTasks()));
    cancelSource.requestCancellation();
    baton.post();
    co_await scope.joinAsync();
  }());
}

TEST_F(CollectAllTryRangeTest, KeepsRequestContextOfChildTasksIndependent) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    folly::RequestContextScopeGuard requestScope;

    auto getContextData = []() {
      return folly::RequestContext::get()->getContextData("test");
    };

    auto setContextData = []() {
      folly::RequestContext::get()->setContextData(
          "test", std::make_unique<TestRequestData>());
    };

    setContextData();
    auto initialContextData = getContextData();

    auto makeChildTask = [&]() -> folly::coro::Task<void> {
      EXPECT_TRUE(getContextData() == initialContextData);
      folly::RequestContextScopeGuard childScope;
      EXPECT_TRUE(getContextData() == nullptr);
      co_await folly::coro::co_reschedule_on_current_executor;
      EXPECT_TRUE(getContextData() == nullptr);
      setContextData();
      auto newContextData = getContextData();
      EXPECT_TRUE(newContextData != nullptr);
      EXPECT_TRUE(newContextData != initialContextData);
      co_await folly::coro::co_reschedule_on_current_executor;
      EXPECT_TRUE(getContextData() == newContextData);
    };

    std::vector<folly::coro::Task<void>> tasks;
    tasks.emplace_back(makeChildTask());
    tasks.emplace_back(makeChildTask());
    tasks.emplace_back(makeChildTask());

    co_await folly::coro::collectAllTryRange(std::move(tasks));

    EXPECT_TRUE(getContextData() == initialContextData);
  }());
}

TEST_F(CollectAllTryRangeTest, GeneratorFromRange) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    folly::coro::CancellableAsyncScope scope;
    auto makeTask = [](int i) -> folly::coro::Task<int> {
      co_await folly::coro::sleep(std::chrono::milliseconds(100 * i));
      co_return i;
    };
    std::vector<folly::coro::Task<int>> tasks;
    for (int i = 5; i > 0; --i) {
      tasks.push_back(makeTask(i));
    }

    auto results =
        folly::coro::makeUnorderedTryAsyncGenerator(scope, std::move(tasks));
    // co_await doesn't work inside EXPECT_EQ
    EXPECT_TRUE(**(co_await results.next()) == 1);
    EXPECT_TRUE(**(co_await results.next()) == 2);
    EXPECT_TRUE(**(co_await results.next()) == 3);
    EXPECT_TRUE(**(co_await results.next()) == 4);
    EXPECT_TRUE(**(co_await results.next()) == 5);
    co_await scope.joinAsync();
  }());
}

TEST_F(CollectAllTryRangeTest, GeneratorFromRangeFailed) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    folly::coro::AsyncScope scope;
    auto makeTask = [](int i) -> folly::coro::Task<int> {
      co_await folly::coro::sleep(std::chrono::milliseconds(200 * i));
      co_return i;
    };
    auto generateTasks =
        [&]() -> folly::coro::Generator<folly::coro::Task<int>&&> {
      co_yield []() -> folly::coro::Task<int> {
        co_await folly::coro::sleep(std::chrono::milliseconds(700));
        co_yield folly::coro::co_error(std::runtime_error("foo"));
      }();
      for (int i = 5; i > 0; --i) {
        co_yield makeTask(i);
      }
    };

    auto results =
        folly::coro::makeUnorderedTryAsyncGenerator(scope, generateTasks());
    // co_await doesn't work inside EXPECT_EQ
    EXPECT_TRUE(**(co_await results.next()) == 1);
    EXPECT_TRUE(**(co_await results.next()) == 2);
    EXPECT_TRUE(**(co_await results.next()) == 3);
    EXPECT_TRUE((co_await results.next())->hasException<std::runtime_error>());
    EXPECT_TRUE(**(co_await results.next()) == 4);
    EXPECT_TRUE(**(co_await results.next()) == 5);
    co_await scope.joinAsync();
  }());
}

TEST_F(CollectAllTryRangeTest, GeneratorFromRangeCancelled) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    folly::coro::AsyncScope scope;
    auto makeTask = [](int i) -> folly::coro::Task<int> {
      co_await folly::coro::sleep(std::chrono::milliseconds(1000 * i));
      co_return i;
    };
    folly::CancellationSource cancelSource;
    auto generateTasks =
        [&]() -> folly::coro::Generator<folly::coro::Task<int>&&> {
      for (int i = 1; i < 10; ++i) {
        co_yield makeTask(i);
        if (i == 4) {
          cancelSource.requestCancellation();
        }
      }
    };
    auto start = std::chrono::steady_clock::now();
    auto results =
        folly::coro::makeUnorderedTryAsyncGenerator(scope, generateTasks());
    auto result = co_await folly::coro::co_withCancellation(
        cancelSource.getToken(), results.next());
    auto end = std::chrono::steady_clock::now();

    EXPECT_LT(end - start, std::chrono::milliseconds(1000));
    EXPECT_TRUE(result->hasException<folly::OperationCancelled>());
    co_await scope.joinAsync();
  }());
}

class CollectAllWindowedTest : public testing::Test {};

TEST_F(CollectAllWindowedTest, ConcurrentTasks) {
  folly::CPUThreadPoolExecutor threadPool{
      4, std::make_shared<folly::NamedThreadFactory>("TestThreadPool")};

  using namespace folly::coro;

  auto results = blockingWait(collectAllWindowed(
      [&]() -> Generator<Task<std::string>&&> {
        for (int i = 0; i < 10'000; ++i) {
          co_yield [](int idx) -> Task<std::string> {
            co_await folly::coro::co_reschedule_on_current_executor;
            co_return folly::to<std::string>(idx);
          }(i);
        }
      }(),
      10));

  EXPECT_EQ(10'000, results.size());
  for (int i = 0; i < 10'000; ++i) {
    EXPECT_EQ(folly::to<std::string>(i), results[i]);
  }
}

TEST_F(CollectAllWindowedTest, WithGeneratorOfTaskOfValue) {
  using namespace std::literals::chrono_literals;

  const std::size_t maxConcurrency = 10;
  std::atomic<int> activeCount{0};
  std::atomic<int> completedCount{0};
  auto makeTask = [&](int index) -> folly::coro::Task<int> {
    auto count = ++activeCount;
    CHECK_LE(count, maxConcurrency);

    // Reschedule a variable number of times so that tasks may complete out of
    // order.
    for (int i = 0; i < index % 5; ++i) {
      co_await folly::coro::co_reschedule_on_current_executor;
    }

    --activeCount;
    ++completedCount;

    co_return index;
  };

  auto makeTaskGenerator =
      [&]() -> folly::coro::Generator<folly::coro::Task<int>&&> {
    for (int i = 0; i < 100; ++i) {
      co_yield makeTask(i);
    }
  };

  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    auto results = co_await folly::coro::collectAllWindowed(
        makeTaskGenerator(), maxConcurrency);
    EXPECT_EQ(100, results.size());
    for (int i = 0; i < 100; ++i) {
      EXPECT_EQ(i, results[i]);
    }
  }());

  EXPECT_EQ(0, activeCount.load());
  EXPECT_EQ(100, completedCount);
}

TEST_F(CollectAllWindowedTest, WithGeneratorOfTaskOfVoid) {
  using namespace std::literals::chrono_literals;

  const std::size_t maxConcurrency = 10;
  std::atomic<int> activeCount{0};
  std::atomic<int> completedCount{0};
  auto makeTask = [&]() -> folly::coro::Task<void> {
    auto count = ++activeCount;
    CHECK_LE(count, maxConcurrency);
    co_await folly::coro::co_reschedule_on_current_executor;
    --activeCount;
    ++completedCount;
  };

  auto makeTaskGenerator =
      [&]() -> folly::coro::Generator<folly::coro::Task<void>&&> {
    for (int i = 0; i < 100; ++i) {
      co_yield makeTask();
    }
  };

  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    co_await folly::coro::collectAllWindowed(
        makeTaskGenerator(), maxConcurrency);
  }());

  EXPECT_EQ(0, activeCount.load());
  EXPECT_EQ(100, completedCount);
}

TEST_F(CollectAllWindowedTest, VectorOfVoidTask) {
  using namespace std::literals::chrono_literals;

  int count = 0;
  auto makeTask = [&]() -> folly::coro::Task<void> {
    co_await folly::coro::co_reschedule_on_current_executor;
    ++count;
  };

  std::vector<folly::coro::Task<void>> tasks;
  for (int i = 0; i < 10; ++i) {
    tasks.push_back(makeTask());
  }

  folly::coro::blockingWait(
      folly::coro::collectAllWindowed(std::move(tasks), 5));

  EXPECT_EQ(10, count);
}

TEST_F(CollectAllWindowedTest, VectorOfValueTask) {
  using namespace std::literals::chrono_literals;

  int count = 0;
  auto makeTask = [&](int i) -> folly::coro::Task<std::unique_ptr<int>> {
    co_await folly::coro::co_reschedule_on_current_executor;
    ++count;
    co_return std::make_unique<int>(i);
  };

  std::vector<folly::coro::Task<std::unique_ptr<int>>> tasks;
  for (int i = 0; i < 10; ++i) {
    tasks.push_back(makeTask(i));
  }

  auto results = folly::coro::blockingWait(
      folly::coro::collectAllWindowed(std::move(tasks), 5));

  EXPECT_EQ(10, count);
  EXPECT_EQ(10, results.size());
  for (int i = 0; i < 10; ++i) {
    EXPECT_EQ(i, *results[i]);
  }
}

TEST_F(CollectAllWindowedTest, MultipleFailuresPropagatesFirstError) {
  try {
    [[maybe_unused]] auto results =
        folly::coro::blockingWait(folly::coro::collectAllWindowed(
            []() -> folly::coro::Generator<folly::coro::Task<int>&&> {
              for (int i = 0; i < 10; ++i) {
                co_yield [](int idx) -> folly::coro::Task<int> {
                  using namespace std::literals::chrono_literals;
                  if (idx == 3) {
                    co_await folly::coro::co_reschedule_on_current_executor;
                    co_await folly::coro::co_reschedule_on_current_executor;
                    throw ErrorA{};
                  } else if (idx == 7) {
                    co_await folly::coro::co_reschedule_on_current_executor;
                    throw ErrorB{};
                  }
                  co_return idx;
                }(i);
              }
            }(),
            5));
    ADD_FAILURE() << "Hit unexpected codepath"; // Should have thrown.
  } catch (const ErrorB&) {
    // Expected.
  }
}

TEST_F(CollectAllWindowedTest, SubtasksCancelledWhenASubtaskFails) {
  using namespace std::chrono_literals;

  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    bool consumedAllTasks = false;
    auto generateTasks =
        [&]() -> folly::coro::Generator<folly::coro::Task<void>&&> {
      co_yield []() -> folly::coro::Task<void> {
        co_await folly::coro::co_reschedule_on_current_executor;
        throw ErrorA{};
      }();

      for (int i = 0; i < 10; ++i) {
        co_yield folly::coro::sleep(10s);
      }

      consumedAllTasks = true;
    };

    auto start = std::chrono::steady_clock::now();
    try {
      co_await folly::coro::collectAllWindowed(generateTasks(), 2);
      ADD_FAILURE() << "Hit unexpected codepath";
    } catch (const ErrorA&) {
    }
    auto end = std::chrono::steady_clock::now();

    EXPECT_LT(end - start, 1s);
    EXPECT_TRUE(consumedAllTasks);
  }());
}

TEST_F(CollectAllWindowedTest, SubtasksCancelledWhenParentTaskCancelled) {
  using namespace std::chrono_literals;

  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    folly::CancellationSource cancelSource;

    bool consumedAllTasks = false;
    auto generateTasks =
        [&]() -> folly::coro::Generator<folly::coro::Task<void>&&> {
      co_yield sleepThatShouldBeCancelled(10s);
      co_yield sleepThatShouldBeCancelled(10s);

      co_yield [&]() -> folly::coro::Task<void> {
        co_await folly::coro::co_reschedule_on_current_executor;
        co_await folly::coro::co_reschedule_on_current_executor;
        co_await folly::coro::co_reschedule_on_current_executor;
        co_await folly::coro::co_reschedule_on_current_executor;
        cancelSource.requestCancellation();

        auto token = co_await folly::coro::co_current_cancellation_token;
        EXPECT_TRUE(token.isCancellationRequested());
      }();

      co_yield sleepThatShouldBeCancelled(10s);
      co_yield sleepThatShouldBeCancelled(10s);

      consumedAllTasks = true;
    };

    auto start = std::chrono::steady_clock::now();
    co_await folly::coro::co_withCancellation(
        cancelSource.getToken(),
        folly::coro::collectAllWindowed(generateTasks(), 4));
    auto end = std::chrono::steady_clock::now();

    EXPECT_LT(end - start, 1s);
    EXPECT_TRUE(consumedAllTasks);
  }());
}

TEST_F(CollectAllWindowedTest, CancellationTokenRemainsActiveAfterReturn) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    folly::CancellationSource cancelSource;
    folly::coro::AsyncScope scope;
    folly::coro::Baton baton;
    auto generateTasks =
        [&]() -> folly::coro::Generator<folly::coro::Task<void>&&> {
      co_yield folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
        auto token = co_await folly::coro::co_current_cancellation_token;
        auto ex = co_await folly::coro::co_current_executor;
        scope.add(
            folly::coro::co_withCancellation(
                token, folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
                  auto innerToken =
                      co_await folly::coro::co_current_cancellation_token;
                  co_await baton;
                  EXPECT_TRUE(innerToken.isCancellationRequested());
                }))
                .scheduleOn(ex));
      });
    };

    co_await folly::coro::co_withCancellation(
        cancelSource.getToken(),
        folly::coro::collectAllWindowed(generateTasks(), 1));
    cancelSource.requestCancellation();
    baton.post();
    co_await scope.joinAsync();
  }());
}

TEST_F(CollectAllWindowedTest, KeepsRequestContextOfChildTasksIndependent) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    folly::RequestContextScopeGuard requestScope;

    auto getContextData = []() {
      return folly::RequestContext::get()->getContextData("test");
    };

    auto setContextData = []() {
      folly::RequestContext::get()->setContextData(
          "test", std::make_unique<TestRequestData>());
    };

    setContextData();
    auto initialContextData = getContextData();

    auto makeChildTask = [&]() -> folly::coro::Task<void> {
      EXPECT_TRUE(getContextData() == initialContextData);
      folly::RequestContextScopeGuard childScope;
      EXPECT_TRUE(getContextData() == nullptr);
      co_await folly::coro::co_reschedule_on_current_executor;
      EXPECT_TRUE(getContextData() == nullptr);
      setContextData();
      auto newContextData = getContextData();
      EXPECT_TRUE(newContextData != nullptr);
      EXPECT_TRUE(newContextData != initialContextData);
      co_await folly::coro::co_reschedule_on_current_executor;
      EXPECT_TRUE(getContextData() == newContextData);
    };

    std::vector<folly::coro::Task<void>> tasks;
    tasks.emplace_back(makeChildTask());
    tasks.emplace_back(makeChildTask());
    tasks.emplace_back(makeChildTask());

    co_await folly::coro::collectAllWindowed(std::move(tasks), 2);

    EXPECT_TRUE(getContextData() == initialContextData);
  }());
}

TEST_F(CollectAllWindowedTest, VectorOfTaskWithExecutorUsage) {
  folly::CPUThreadPoolExecutor threadPool{
      4, std::make_shared<folly::NamedThreadFactory>("TestThreadPool")};

  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    std::vector<folly::coro::TaskWithExecutor<int>> tasks;
    for (int i = 0; i < 4; ++i) {
      tasks.push_back(
          [](int idx) -> folly::coro::Task<int> { co_return idx + 1; }(i)
                             .scheduleOn(&threadPool));
    }

    auto results =
        co_await folly::coro::collectAllWindowed(std::move(tasks), 2);
    EXPECT_TRUE(results.size() == 4);
    EXPECT_TRUE(results[0] == 1);
    EXPECT_TRUE(results[1] == 2);
    EXPECT_TRUE(results[2] == 3);
    EXPECT_TRUE(results[3] == 4);
  }());
}

class CollectAllTryWindowedTest : public testing::Test {};

TEST_F(CollectAllTryWindowedTest, PartialFailure) {
  auto results = folly::coro::blockingWait(folly::coro::collectAllTryWindowed(
      []() -> folly::coro::Generator<folly::coro::Task<int>&&> {
        for (int i = 0; i < 10; ++i) {
          co_yield [](int idx) -> folly::coro::Task<int> {
            using namespace std::literals::chrono_literals;
            if (idx == 3) {
              co_await folly::coro::co_reschedule_on_current_executor;
              co_await folly::coro::co_reschedule_on_current_executor;
              throw ErrorA{};
            } else if (idx == 7) {
              co_await folly::coro::co_reschedule_on_current_executor;
              throw ErrorB{};
            }
            co_return idx;
          }(i);
        }
      }(),
      5));
  EXPECT_EQ(10, results.size());

  for (int i = 0; i < 10; ++i) {
    if (i == 3) {
      EXPECT_TRUE(results[i].hasException());
      EXPECT_TRUE(results[i].exception().is_compatible_with<ErrorA>());
    } else if (i == 7) {
      EXPECT_TRUE(results[i].hasException());
      EXPECT_TRUE(results[i].exception().is_compatible_with<ErrorB>());
    } else {
      EXPECT_TRUE(results[i].hasValue());
      EXPECT_EQ(i, results[i].value());
    }
  }
}

TEST_F(CollectAllTryWindowedTest, GeneratorFailure) {
  int active = 0;
  int started = 0;
  auto makeTask = [&](int i) -> folly::coro::Task<void> {
    ++active;
    ++started;
    for (int j = 0; j < (i % 3); ++j) {
      co_await folly::coro::co_reschedule_on_current_executor;
    }
    --active;
  };

  auto generateTasks =
      [&]() -> folly::coro::Generator<folly::coro::Task<void>&&> {
    for (int i = 0; i < 10; ++i) {
      co_yield makeTask(i);
    }
    throw ErrorA{};
  };

  try {
    [[maybe_unused]] auto results = folly::coro::blockingWait(
        folly::coro::collectAllTryWindowed(generateTasks(), 5));
    ADD_FAILURE() << "Hit unexpected codepath";
  } catch (const ErrorA&) {
  }

  // Even if the generator throws an exception we should still have launched
  // and waited for completion all of the prior tasks in the sequence.
  EXPECT_EQ(10, started);
  EXPECT_EQ(0, active);
}

TEST_F(CollectAllTryWindowedTest, NotCancelledWhenSubtaskFails) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    auto generateTasks =
        [&]() -> folly::coro::Generator<folly::coro::Task<void>&&> {
      co_yield []() -> folly::coro::Task<void> {
        co_await folly::coro::co_reschedule_on_current_executor;
        throw ErrorA{};
      }();

      auto makeValidationTask = []() -> folly::coro::Task<void> {
        co_await folly::coro::co_reschedule_on_current_executor;
        co_await folly::coro::co_reschedule_on_current_executor;
        co_await folly::coro::co_reschedule_on_current_executor;
        auto token = co_await folly::coro::co_current_cancellation_token;
        EXPECT_FALSE(token.isCancellationRequested());
      };

      co_yield makeValidationTask();
      co_yield makeValidationTask();
    };

    auto results =
        co_await folly::coro::collectAllTryWindowed(generateTasks(), 2);
    EXPECT_EQ(3, results.size());
    EXPECT_TRUE(results[0].hasException());
    EXPECT_TRUE(results[1].hasValue());
    EXPECT_TRUE(results[2].hasValue());
  }());
}

TEST_F(CollectAllTryWindowedTest, SubtasksCancelledWhenParentTaskCancelled) {
  using namespace std::chrono_literals;

  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    folly::CancellationSource cancelSource;

    bool consumedAllTasks = false;
    auto generateTasks =
        [&]() -> folly::coro::Generator<folly::coro::Task<void>&&> {
      co_yield sleepThatShouldBeCancelled(10s);
      co_yield sleepThatShouldBeCancelled(10s);

      co_yield [&]() -> folly::coro::Task<void> {
        co_await folly::coro::co_reschedule_on_current_executor;
        co_await folly::coro::co_reschedule_on_current_executor;
        co_await folly::coro::co_reschedule_on_current_executor;
        co_await folly::coro::co_reschedule_on_current_executor;
        cancelSource.requestCancellation();

        auto token = co_await folly::coro::co_current_cancellation_token;
        EXPECT_TRUE(token.isCancellationRequested());
      }();

      co_yield sleepThatShouldBeCancelled(10s);
      co_yield sleepThatShouldBeCancelled(10s);

      consumedAllTasks = true;
    };

    auto start = std::chrono::steady_clock::now();
    auto results = co_await folly::coro::co_withCancellation(
        cancelSource.getToken(),
        folly::coro::collectAllTryWindowed(generateTasks(), 4));
    auto end = std::chrono::steady_clock::now();

    EXPECT_EQ(5, results.size());
    for (auto& result : results) {
      EXPECT_TRUE(result.hasValue());
    }
    EXPECT_LT(end - start, 1s);
    EXPECT_TRUE(consumedAllTasks);
  }());
}

TEST_F(CollectAllTryWindowedTest, CancellationTokenRemainsActiveAfterReturn) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    folly::CancellationSource cancelSource;
    folly::coro::AsyncScope scope;
    folly::coro::Baton baton;
    auto generateTasks =
        [&]() -> folly::coro::Generator<folly::coro::Task<void>&&> {
      co_yield folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
        auto token = co_await folly::coro::co_current_cancellation_token;
        auto ex = co_await folly::coro::co_current_executor;
        scope.add(
            folly::coro::co_withCancellation(
                token, folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
                  auto innerToken =
                      co_await folly::coro::co_current_cancellation_token;
                  co_await baton;
                  EXPECT_TRUE(innerToken.isCancellationRequested());
                }))
                .scheduleOn(ex));
      });
    };

    co_await folly::coro::co_withCancellation(
        cancelSource.getToken(),
        folly::coro::collectAllTryWindowed(generateTasks(), 1));
    cancelSource.requestCancellation();
    baton.post();
    co_await scope.joinAsync();
  }());
}

class CollectAnyTest : public testing::Test {};

TEST_F(CollectAnyTest, OneTaskWithValue) {
  folly::coro::Baton baton;
  auto f = [&]() -> folly::coro::Task<std::string> {
    co_await baton;
    co_return "hello";
  };

  bool completed = false;
  auto run = [&]() -> folly::coro::Task<void> {
    auto [index, result] = co_await folly::coro::collectAny(f());
    EXPECT_EQ("hello", result.value());
    EXPECT_EQ(0, index);
    completed = true;
  };

  folly::ManualExecutor executor;

  auto future = run().scheduleOn(&executor).start();

  executor.drain();

  EXPECT_FALSE(completed);

  baton.post();

  // Posting the baton should have just scheduled the 'f()' coroutine
  // for resumption on the executor but should not have executed
  // until we drain the executor again.
  EXPECT_FALSE(completed);

  executor.drain();

  EXPECT_TRUE(completed);
  EXPECT_TRUE(future.isReady());
}

TEST_F(CollectAnyTest, OneVoidTask) {
  bool completed = false;
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    // Checks that the task actually runs and that 'void' results are
    // promoted to folly::Unit when placed in a tuple.
    std::pair<std::size_t, folly::Try<void>> result =
        co_await folly::coro::collectAny([&]() -> folly::coro::Task<void> {
          completed = true;
          co_return;
        }());
    (void)result;
  }());
  EXPECT_TRUE(completed);
}

TEST_F(CollectAnyTest, MoveOnlyType) {
  bool completed = false;
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    // Checks that the task actually runs and that move only results
    // can be correctly returned
    std::pair<std::size_t, folly::Try<std::unique_ptr<int>>> result =
        co_await folly::coro::collectAny(
            [&]() -> folly::coro::Task<std::unique_ptr<int>> {
              completed = true;
              co_return std::make_unique<int>(1);
            }());
    (void)result;
  }());
  EXPECT_TRUE(completed);
}

TEST_F(CollectAnyTest, CollectAnyDoesntCompleteUntilAllTasksComplete) {
  folly::coro::Baton baton1;
  folly::coro::Baton baton2;
  bool task1Started = false;
  bool task2Started = false;
  bool complete = false;
  constexpr std::size_t taskTerminatingExpectedIndex = 1;

  auto run = [&]() -> folly::coro::Task<void> {
    auto [index, result] = co_await folly::coro::collectAny(
        [&]() -> folly::coro::Task<int> {
          task1Started = true;
          co_await baton1;
          co_return 42;
        }(),
        [&]() -> folly::coro::Task<int> {
          task2Started = true;
          co_await baton2;
          co_return 314;
        }());
    complete = true;
    EXPECT_EQ(taskTerminatingExpectedIndex, index);
    EXPECT_EQ(result.value(), 314);
  };

  folly::ManualExecutor executor;

  auto future = run().scheduleOn(&executor).start();

  EXPECT_FALSE(task1Started);
  EXPECT_FALSE(task2Started);

  executor.drain();

  EXPECT_TRUE(task1Started);
  EXPECT_TRUE(task2Started);
  EXPECT_FALSE(complete);
  baton2.post();
  executor.drain();
  EXPECT_FALSE(complete);
  baton1.post();
  executor.drain();
  EXPECT_TRUE(complete);
  EXPECT_TRUE(future.isReady());
}

TEST_F(CollectAnyTest, ThrowsFirstError) {
  bool caughtException = false;
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    bool throwError = true;
    // Child tasks are started in-order.
    // The first task will reschedule itself onto the executor.
    // The second task will fail immediately and will be the first
    // task to fail.
    // Then the third and first tasks will fail.
    // As the second task failed first we should see its exception
    // propagate out of collectAny().
    auto [index, result] = co_await folly::coro::collectAny(
        [&]() -> folly::coro::Task<int> {
          co_await folly::coro::co_reschedule_on_current_executor;
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
    EXPECT_EQ(1, index);
    try {
      result.value();
      ADD_FAILURE() << "Hit unexpected codepath";
    } catch (const ErrorB&) {
      caughtException = true;
    }
  }());
  EXPECT_TRUE(caughtException);
}

TEST_F(CollectAnyTest, CollectAnyCancelsSubtasksWhenASubtaskCompletes) {
  using namespace std::chrono_literals;

  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    auto start = std::chrono::steady_clock::now();

    auto [index, result] = co_await folly::coro::collectAny(
        []() -> folly::coro::Task<int> {
          co_await sleepThatShouldBeCancelled(10s);
          co_return 42;
        }(),
        []() -> folly::coro::Task<int> {
          co_await sleepThatShouldBeCancelled(5s);
          co_return 314;
        }(),
        []() -> folly::coro::Task<int> {
          co_await folly::coro::co_reschedule_on_current_executor;
          throw ErrorA{};
        }());
    EXPECT_EQ(2, index);
    try {
      result.value();
      ADD_FAILURE() << "Hit unexpected codepath";
    } catch (const ErrorA&) {
    }
    auto end = std::chrono::steady_clock::now();

    EXPECT_LT(end - start, 1s);
  }());
}

TEST_F(CollectAnyTest, CollectAnyCancelsSubtasksWhenParentTaskCancelled) {
  using namespace std::chrono_literals;

  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    auto start = std::chrono::steady_clock::now();
    folly::CancellationSource cancelSource;
    auto [index, result] = co_await folly::coro::co_withCancellation(
        cancelSource.getToken(),
        folly::coro::collectAny(
            [&]() -> folly::coro::Task<int> {
              co_await sleepThatShouldBeCancelled(10s);
              co_return 42;
            }(),
            [&]() -> folly::coro::Task<int> {
              co_await sleepThatShouldBeCancelled(5s);
              co_return 314;
            }(),
            [&]() -> folly::coro::Task<int> {
              co_await folly::coro::co_reschedule_on_current_executor;
              co_await folly::coro::co_reschedule_on_current_executor;
              co_await folly::coro::co_reschedule_on_current_executor;
              cancelSource.requestCancellation();
              co_await sleepThatShouldBeCancelled(15s);
              co_return 123;
            }()));
    auto end = std::chrono::steady_clock::now();
    EXPECT_LT(end - start, 1s);
  }());
}

TEST_F(CollectAnyTest, CancellationTokenRemainsActiveAfterReturn) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    folly::CancellationSource cancelSource;
    folly::coro::AsyncScope scope;
    folly::coro::Baton baton;
    auto task = folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
      auto token = co_await folly::coro::co_current_cancellation_token;
      auto ex = co_await folly::coro::co_current_executor;
      scope.add(
          folly::coro::co_withCancellation(
              token, folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
                auto innerToken =
                    co_await folly::coro::co_current_cancellation_token;
                co_await baton;
                EXPECT_TRUE(innerToken.isCancellationRequested());
              }))
              .scheduleOn(ex));
    });

    co_await folly::coro::co_withCancellation(
        cancelSource.getToken(), folly::coro::collectAny(std::move(task)));
    cancelSource.requestCancellation();
    baton.post();
    co_await scope.joinAsync();
  }());
}

class CollectAnyWithoutExceptionTest : public testing::Test {};

TEST_F(CollectAnyWithoutExceptionTest, OneTaskWithValue) {
  folly::coro::Baton baton;
  auto f = [&]() -> folly::coro::Task<std::string> {
    co_await baton;
    co_return "hello";
  };

  bool completed = false;
  auto run = [&]() -> folly::coro::Task<void> {
    auto [index, result] =
        co_await folly::coro::collectAnyWithoutException(f());
    EXPECT_EQ("hello", result.value());
    EXPECT_EQ(0, index);
    completed = true;
  };

  folly::ManualExecutor executor;

  auto future = run().scheduleOn(&executor).start();

  executor.drain();

  EXPECT_FALSE(completed);

  baton.post();

  // Posting the baton should have just scheduled the 'f()' coroutine
  // for resumption on the executor but should not have executed
  // until we drain the executor again.
  EXPECT_FALSE(completed);

  executor.drain();

  EXPECT_TRUE(completed);
  EXPECT_TRUE(future.isReady());
}

TEST_F(CollectAnyWithoutExceptionTest, OneVoidTask) {
  bool completed = false;
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    // Checks that the task actually runs and that 'void' results are
    // promoted to folly::Unit when placed in a tuple.
    std::pair<std::size_t, folly::Try<void>> result =
        co_await folly::coro::collectAnyWithoutException(
            [&]() -> folly::coro::Task<void> {
              completed = true;
              co_return;
            }());
    (void)result;
  }());
  EXPECT_TRUE(completed);
}

TEST_F(CollectAnyWithoutExceptionTest, MoveOnlyType) {
  bool completed = false;
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    // Checks that the task actually runs and that move only results
    // can be correctly returned
    std::pair<std::size_t, folly::Try<std::unique_ptr<int>>> result =
        co_await folly::coro::collectAnyWithoutException(
            [&]() -> folly::coro::Task<std::unique_ptr<int>> {
              completed = true;
              co_return std::make_unique<int>(1);
            }());
    (void)result;
  }());
  EXPECT_TRUE(completed);
}

TEST_F(CollectAnyWithoutExceptionTest, DoesntCompleteUntilAllTasksComplete) {
  folly::coro::Baton baton1;
  folly::coro::Baton baton2;
  bool task1Started = false;
  bool task2Started = false;
  bool complete = false;
  constexpr std::size_t taskTerminatingExpectedIndex = 1;

  auto run = [&]() -> folly::coro::Task<void> {
    auto [index, result] = co_await folly::coro::collectAnyWithoutException(
        [&]() -> folly::coro::Task<int> {
          task1Started = true;
          co_await baton1;
          co_return 42;
        }(),
        [&]() -> folly::coro::Task<int> {
          task2Started = true;
          co_await baton2;
          co_return 314;
        }());
    complete = true;
    EXPECT_EQ(taskTerminatingExpectedIndex, index);
    EXPECT_EQ(result.value(), 314);
  };

  folly::ManualExecutor executor;

  auto future = run().scheduleOn(&executor).start();

  EXPECT_FALSE(task1Started);
  EXPECT_FALSE(task2Started);

  executor.drain();

  EXPECT_TRUE(task1Started);
  EXPECT_TRUE(task2Started);
  EXPECT_FALSE(complete);
  baton2.post();
  executor.drain();
  EXPECT_FALSE(complete);
  baton1.post();
  executor.drain();
  EXPECT_TRUE(complete);
  EXPECT_TRUE(future.isReady());
}

TEST_F(CollectAnyWithoutExceptionTest, ThrowsLastError) {
  bool caughtException = false;
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    bool throwError = true;
    // Child tasks are started in-order.
    // Since the third task fails last we should see its exception
    // propagate out of collectAnyWithoutException().
    auto [index, result] = co_await folly::coro::collectAnyWithoutException(
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
    EXPECT_EQ(2, index);
    try {
      result.value();
      ADD_FAILURE() << "Hit unexpected codepath";
    } catch (const ErrorC&) {
      caughtException = true;
    }
  }());
  EXPECT_TRUE(caughtException);
}

TEST_F(CollectAnyWithoutExceptionTest, ReturnsFirstValueWithoutException) {
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    // Child tasks are started in-order.
    // Since the third task returns a value first ignore the exceptions and the
    // last value and propagate the third value out of
    // collectAnyWithoutException().
    auto [index, result] = co_await folly::coro::collectAnyWithoutException(
        []() -> folly::coro::Task<int> {
          throw ErrorA{};
          co_return 1;
        }(),
        []() -> folly::coro::Task<int> {
          throw ErrorB{};
          co_return 2;
        }(),
        []() -> folly::coro::Task<int> { co_return 3; }(),
        []() -> folly::coro::Task<int> { co_return 4; }());
    EXPECT_EQ(2, index);
    EXPECT_TRUE(result.hasValue());
    EXPECT_EQ(3, result.value());
  }());
}

TEST_F(CollectAnyWithoutExceptionTest, CancelsSubtasksWhenASubtaskCompletes) {
  using namespace std::chrono_literals;

  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    auto start = std::chrono::steady_clock::now();

    auto [index, result] = co_await folly::coro::collectAnyWithoutException(
        []() -> folly::coro::Task<int> {
          co_await sleepThatShouldBeCancelled(10s);
          co_return 42;
        }(),
        []() -> folly::coro::Task<int> {
          co_await sleepThatShouldBeCancelled(5s);
          co_return 314;
        }(),
        []() -> folly::coro::Task<int> {
          co_await folly::coro::co_reschedule_on_current_executor;
          co_return 1;
        }());
    EXPECT_EQ(2, index);
    EXPECT_TRUE(result.hasValue());
    EXPECT_EQ(1, result.value());
    auto end = std::chrono::steady_clock::now();

    EXPECT_LT(end - start, 1s);
  }());
}

TEST_F(CollectAnyWithoutExceptionTest, CancelsSubtasksWhenParentTaskCancelled) {
  using namespace std::chrono_literals;

  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    auto start = std::chrono::steady_clock::now();
    folly::CancellationSource cancelSource;
    auto [index, result] = co_await folly::coro::co_withCancellation(
        cancelSource.getToken(),
        folly::coro::collectAnyWithoutException(
            [&]() -> folly::coro::Task<int> {
              co_await sleepThatShouldBeCancelled(10s);
              co_return 42;
            }(),
            [&]() -> folly::coro::Task<int> {
              co_await sleepThatShouldBeCancelled(5s);
              co_return 314;
            }(),
            [&]() -> folly::coro::Task<int> {
              co_await folly::coro::co_reschedule_on_current_executor;
              co_await folly::coro::co_reschedule_on_current_executor;
              co_await folly::coro::co_reschedule_on_current_executor;
              cancelSource.requestCancellation();
              co_await sleepThatShouldBeCancelled(15s);
              co_return 123;
            }()));
    auto end = std::chrono::steady_clock::now();
    EXPECT_LT(end - start, 1s);
  }());
}

TEST_F(
    CollectAnyWithoutExceptionTest, CancellationTokenRemainsActiveAfterReturn) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    folly::CancellationSource cancelSource;
    folly::coro::AsyncScope scope;
    folly::coro::Baton baton;
    auto task = folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
      auto token = co_await folly::coro::co_current_cancellation_token;
      auto ex = co_await folly::coro::co_current_executor;
      scope.add(
          folly::coro::co_withCancellation(
              token, folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
                auto innerToken =
                    co_await folly::coro::co_current_cancellation_token;
                co_await baton;
                EXPECT_TRUE(innerToken.isCancellationRequested());
              }))
              .scheduleOn(ex));
    });

    co_await folly::coro::co_withCancellation(
        cancelSource.getToken(),
        folly::coro::collectAnyWithoutException(std::move(task)));
    cancelSource.requestCancellation();
    baton.post();
    co_await scope.joinAsync();
  }());
}

class CollectAnyNoDiscardTest : public testing::Test {};

TEST_F(CollectAnyNoDiscardTest, OneTask) {
  auto value = [&]() -> folly::coro::Task<std::string> { co_return "hello"; };
  auto throws = [&]() -> folly::coro::Task<std::string> {
    co_yield folly::coro::co_error(ErrorA{});
  };

  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    auto [result] = co_await folly::coro::collectAnyNoDiscard(value());
    EXPECT_TRUE(result.hasValue());
    EXPECT_EQ("hello", result.value());
    std::tie(result) = co_await folly::coro::collectAnyNoDiscard(throws());
    EXPECT_TRUE(result.hasException<ErrorA>());
  }());
}

TEST_F(CollectAnyNoDiscardTest, MultipleTasksWithValues) {
  std::atomic_size_t count{0};

  // Busy wait until all threads have started before returning
  auto busyWait = [](std::atomic_size_t& count,
                     size_t num) -> folly::coro::Task<void> {
    count.fetch_add(1);
    while (count.load() < num) {
      // Need to yield because collectAnyNoDiscard() won't start the second and
      // third coroutines until the first one gets to a suspend point
      co_await folly::coro::co_reschedule_on_current_executor;
    }
  };

  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    auto [first, second, third] = co_await folly::coro::collectAnyNoDiscard(
        busyWait(count, 3), busyWait(count, 3), busyWait(count, 3));
    EXPECT_TRUE(first.hasValue());
    EXPECT_TRUE(second.hasValue());
    EXPECT_TRUE(third.hasValue());
  }());
}

TEST_F(CollectAnyNoDiscardTest, OneVoidTask) {
  bool completed = false;
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    // Checks that the task actually runs and that 'void' results are
    // promoted to folly::Unit when placed in a tuple.
    std::tuple<folly::Try<void>> result =
        co_await folly::coro::collectAnyNoDiscard(
            [&]() -> folly::coro::Task<void> {
              completed = true;
              co_return;
            }());
    (void)result;
  }());
  EXPECT_TRUE(completed);
}

TEST_F(CollectAnyNoDiscardTest, DoesntCompleteUntilAllTasksComplete) {
  folly::coro::Baton baton1;
  folly::coro::Baton baton2;
  bool task1Started = false;
  bool task2Started = false;
  bool complete = false;

  auto run = [&]() -> folly::coro::Task<void> {
    auto [first, second] = co_await folly::coro::collectAnyNoDiscard(
        [&]() -> folly::coro::Task<int> {
          task1Started = true;
          co_await baton1;
          co_return 42;
        }(),
        [&]() -> folly::coro::Task<int> {
          task2Started = true;
          co_await baton2;
          co_return 314;
        }());
    complete = true;
    EXPECT_TRUE(first.hasValue());
    EXPECT_EQ(first.value(), 42);
    EXPECT_TRUE(second.hasValue());
    EXPECT_EQ(second.value(), 314);
  };

  folly::ManualExecutor executor;

  auto future = run().scheduleOn(&executor).start();

  EXPECT_FALSE(task1Started);
  EXPECT_FALSE(task2Started);

  executor.drain();

  EXPECT_TRUE(task1Started);
  EXPECT_TRUE(task2Started);
  EXPECT_FALSE(complete);
  baton2.post();
  executor.drain();
  EXPECT_FALSE(complete);
  baton1.post();
  executor.drain();
  EXPECT_TRUE(complete);
  EXPECT_TRUE(future.isReady());
}

TEST_F(CollectAnyNoDiscardTest, ThrowsAllErrors) {
  using namespace std::chrono_literals;

  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    // Child tasks are started in-order.
    // The first task will reschedule itself onto the executor.
    // The second task will fail immediately and will be the first task to fail.
    // Then the third and first tasks will fail.
    // We should see all exceptions propagate out of collectAnyNoDiscard().
    auto [first, second, third] = co_await folly::coro::collectAnyNoDiscard(
        [&]() -> folly::coro::Task<int> {
          co_await folly::coro::co_reschedule_on_current_executor;
          co_yield folly::coro::co_error(ErrorA{});
        }(),
        [&]() -> folly::coro::Task<int> {
          co_yield folly::coro::co_error(ErrorB{});
        }(),
        [&]() -> folly::coro::Task<int> {
          co_yield folly::coro::co_error(ErrorC{});
        }());
    EXPECT_TRUE(first.hasException<ErrorA>());
    EXPECT_TRUE(second.hasException<ErrorB>());
    EXPECT_TRUE(third.hasException<ErrorC>());
  }());
}

TEST_F(CollectAnyNoDiscardTest, CancelSubtasksWhenASubtaskCompletes) {
  using namespace std::chrono_literals;

  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    auto start = std::chrono::steady_clock::now();

    auto [first, second, third] = co_await folly::coro::collectAnyNoDiscard(
        []() -> folly::coro::Task<int> {
          co_await folly::coro::sleep(10s);
          co_return 42;
        }(),
        []() -> folly::coro::Task<int> {
          co_await folly::coro::sleep(5s);
          co_return 314;
        }(),
        []() -> folly::coro::Task<int> {
          co_await folly::coro::co_reschedule_on_current_executor;
          co_yield folly::coro::co_error(ErrorA{});
        }());
    EXPECT_TRUE(first.hasException<folly::OperationCancelled>());
    EXPECT_TRUE(second.hasException<folly::OperationCancelled>());
    EXPECT_TRUE(third.hasException<ErrorA>());

    auto end = std::chrono::steady_clock::now();

    EXPECT_LT(end - start, 1s);
  }());
}

TEST_F(CollectAnyNoDiscardTest, CancelSubtasksWhenParentTaskCancelled) {
  using namespace std::chrono_literals;

  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    auto start = std::chrono::steady_clock::now();
    folly::CancellationSource cancelSource;
    auto [first, second, third] = co_await folly::coro::co_withCancellation(
        cancelSource.getToken(),
        folly::coro::collectAnyNoDiscard(
            [&]() -> folly::coro::Task<int> {
              co_await folly::coro::sleep(10s);
              co_return 42;
            }(),
            [&]() -> folly::coro::Task<int> {
              co_await folly::coro::sleep(5s);
              co_return 314;
            }(),
            [&]() -> folly::coro::Task<int> {
              co_await folly::coro::co_reschedule_on_current_executor;
              co_await folly::coro::co_reschedule_on_current_executor;
              co_await folly::coro::co_reschedule_on_current_executor;
              cancelSource.requestCancellation();
              co_await folly::coro::sleep(15s);
              co_return 123;
            }()));
    auto end = std::chrono::steady_clock::now();
    EXPECT_LT(end - start, 1s);
    EXPECT_TRUE(first.hasException<folly::OperationCancelled>());
    EXPECT_TRUE(second.hasException<folly::OperationCancelled>());
    EXPECT_TRUE(third.hasException<folly::OperationCancelled>());
  }());
}

TEST_F(CollectAnyNoDiscardTest, CancellationTokenRemainsActiveAfterReturn) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    folly::CancellationSource cancelSource;
    folly::coro::AsyncScope scope;
    folly::coro::Baton baton;
    auto task = folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
      auto token = co_await folly::coro::co_current_cancellation_token;
      auto ex = co_await folly::coro::co_current_executor;
      scope.add(
          folly::coro::co_withCancellation(
              token, folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
                auto innerToken =
                    co_await folly::coro::co_current_cancellation_token;
                co_await baton;
                EXPECT_TRUE(innerToken.isCancellationRequested());
              }))
              .scheduleOn(ex));
    });

    co_await folly::coro::co_withCancellation(
        cancelSource.getToken(),
        folly::coro::collectAnyNoDiscard(std::move(task)));
    cancelSource.requestCancellation();
    baton.post();
    co_await scope.joinAsync();
  }());
}

class CollectAnyRangeTest : public testing::Test {};

TEST_F(CollectAnyRangeTest, OneTaskWithValue) {
  folly::coro::Baton baton;
  auto f = [&]() -> folly::coro::Task<std::string> {
    co_await baton;
    co_return "hello";
  };

  bool completed = false;
  auto run = [&]() -> folly::coro::Task<void> {
    std::vector<folly::coro::Task<std::string>> tasks;
    tasks.push_back(f());
    auto [index, result] =
        co_await folly::coro::collectAnyRange(std::move(tasks));
    EXPECT_EQ("hello", result.value());
    EXPECT_EQ(0, index);
    completed = true;
  };

  folly::ManualExecutor executor;

  auto future = run().scheduleOn(&executor).start();

  executor.drain();

  EXPECT_FALSE(completed);

  baton.post();

  // Posting the baton should have just scheduled the 'f()' coroutine
  // for resumption on the executor but should not have executed
  // until we drain the executor again.
  EXPECT_FALSE(completed);

  executor.drain();

  EXPECT_TRUE(completed);
  EXPECT_TRUE(future.isReady());
}

TEST_F(CollectAnyRangeTest, OneVoidTask) {
  bool completed = false;
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    // Checks that the task actually runs and that 'void' results are
    // promoted to folly::Unit when placed in a tuple.
    auto generateTasks =
        [&]() -> folly::coro::Generator<folly::coro::Task<void>&&> {
      co_yield [&]() -> folly::coro::Task<void> {
        completed = true;
        co_return;
      }();
    };
    std::pair<std::size_t, folly::Try<void>> result =
        co_await folly::coro::collectAnyRange(generateTasks());
    (void)result;
  }());
  EXPECT_TRUE(completed);
}

TEST_F(CollectAnyRangeTest, MoveOnlyType) {
  bool completed = false;
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    // Checks that the task actually runs and that move only results
    // can be correctly returned
    auto generateTasks = [&]()
        -> folly::coro::Generator<folly::coro::Task<std::unique_ptr<int>>&&> {
      co_yield [&]() -> folly::coro::Task<std::unique_ptr<int>> {
        completed = true;
        co_return std::make_unique<int>(1);
      }();
    };
    std::pair<std::size_t, folly::Try<std::unique_ptr<int>>> result =
        co_await folly::coro::collectAnyRange(generateTasks());
    (void)result;
  }());
  EXPECT_TRUE(completed);
}

TEST_F(CollectAnyRangeTest, CollectAnyDoesntCompleteUntilAllTasksComplete) {
  folly::coro::Baton baton1;
  folly::coro::Baton baton2;
  bool task1Started = false;
  bool task2Started = false;
  bool complete = false;
  constexpr std::size_t taskTerminatingExpectedIndex = 1;

  auto generateTasks =
      [&]() -> folly::coro::Generator<folly::coro::Task<int>&&> {
    auto t1 = [&]() -> folly::coro::Task<int> {
      task1Started = true;
      co_await baton1;
      co_return 42;
    };
    co_yield t1();
    auto t2 = [&]() -> folly::coro::Task<int> {
      task2Started = true;
      co_await baton2;
      co_return 314;
    };
    co_yield t2();
  };

  auto run = [&]() -> folly::coro::Task<void> {
    auto [index, result] =
        co_await folly::coro::collectAnyRange(generateTasks());
    complete = true;
    EXPECT_EQ(taskTerminatingExpectedIndex, index);
    EXPECT_EQ(result.value(), 314);
  };

  folly::ManualExecutor executor;

  auto future = run().scheduleOn(&executor).start();

  EXPECT_FALSE(task1Started);
  EXPECT_FALSE(task2Started);

  executor.drain();

  EXPECT_TRUE(task1Started);
  EXPECT_TRUE(task2Started);
  EXPECT_FALSE(complete);
  baton2.post();
  executor.drain();
  EXPECT_FALSE(complete);
  baton1.post();
  executor.drain();
  EXPECT_TRUE(complete);
  EXPECT_TRUE(future.isReady());
}

TEST_F(CollectAnyRangeTest, ThrowsFirstError) {
  bool caughtException = false;

  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    bool throwError = true;

    auto generateTasks =
        [&]() -> folly::coro::Generator<folly::coro::Task<int>&&> {
      co_yield [&]() -> folly::coro::Task<int> {
        co_await folly::coro::co_reschedule_on_current_executor;
        if (throwError) {
          throw ErrorA{};
        }
        co_return 1;
      }();
      co_yield [&]() -> folly::coro::Task<int> {
        if (throwError) {
          throw ErrorB{};
        }
        co_return 2;
      }();
      co_yield [&]() -> folly::coro::Task<int> {
        if (throwError) {
          throw ErrorC{};
        }
        co_return 3;
      }();
    };
    // Child tasks are started in-order.
    // The first task will reschedule itself onto the executor.
    // The second task will fail immediately and will be the first
    // task to fail.
    // Then the third and first tasks will fail.
    // As the second task failed first we should see its exception
    // propagate out of collectAny().
    auto [index, result] =
        co_await folly::coro::collectAnyRange(generateTasks());
    EXPECT_EQ(1, index);
    try {
      result.value();
      ADD_FAILURE() << "Hit unexpected codepath";
    } catch (const ErrorB&) {
      caughtException = true;
    }
  }());
  EXPECT_TRUE(caughtException);
}

TEST_F(CollectAnyRangeTest, CollectAnyCancelsSubtasksWhenASubtaskCompletes) {
  using namespace std::chrono_literals;

  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    auto start = std::chrono::steady_clock::now();
    std::vector<folly::coro::Task<int>> tasks;
    tasks.push_back([]() -> folly::coro::Task<int> {
      co_await sleepThatShouldBeCancelled(10s);
      co_return 42;
    }());
    tasks.push_back([]() -> folly::coro::Task<int> {
      co_await sleepThatShouldBeCancelled(5s);
      co_return 314;
    }());
    tasks.push_back([]() -> folly::coro::Task<int> {
      co_await folly::coro::co_reschedule_on_current_executor;
      throw ErrorA{};
    }());

    auto [index, result] =
        co_await folly::coro::collectAnyRange(std::move(tasks));
    EXPECT_EQ(2, index);
    try {
      result.value();
      ADD_FAILURE() << "Hit unexpected codepath";
    } catch (const ErrorA&) {
    }
    auto end = std::chrono::steady_clock::now();

    EXPECT_LT(end - start, 1s);
  }());
}

TEST_F(CollectAnyRangeTest, CollectAnyCancelsSubtasksWhenParentTaskCancelled) {
  using namespace std::chrono_literals;

  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    auto start = std::chrono::steady_clock::now();
    folly::CancellationSource cancelSource;
    auto generateTasks =
        [&]() -> folly::coro::Generator<folly::coro::Task<int>&&> {
      co_yield [&]() -> folly::coro::Task<int> {
        co_await sleepThatShouldBeCancelled(10s);
        co_return 42;
      }();
      co_yield [&]() -> folly::coro::Task<int> {
        co_await sleepThatShouldBeCancelled(5s);
        co_return 314;
      }();
      co_yield [&]() -> folly::coro::Task<int> {
        co_await folly::coro::co_reschedule_on_current_executor;
        co_await folly::coro::co_reschedule_on_current_executor;
        co_await folly::coro::co_reschedule_on_current_executor;
        cancelSource.requestCancellation();
        co_await sleepThatShouldBeCancelled(15s);
        co_return 123;
      }();
    };
    auto [index, result] = co_await folly::coro::co_withCancellation(
        cancelSource.getToken(), folly::coro::collectAnyRange(generateTasks()));
    auto end = std::chrono::steady_clock::now();
    EXPECT_LT(end - start, 1s);
  }());
}

TEST_F(CollectAnyRangeTest, CancellationTokenRemainsActiveAfterReturn) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    folly::CancellationSource cancelSource;
    folly::coro::AsyncScope scope;
    folly::coro::Baton baton;
    auto task = folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
      auto token = co_await folly::coro::co_current_cancellation_token;
      auto ex = co_await folly::coro::co_current_executor;
      scope.add(
          folly::coro::co_withCancellation(
              token, folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
                auto innerToken =
                    co_await folly::coro::co_current_cancellation_token;
                co_await baton;
                EXPECT_TRUE(innerToken.isCancellationRequested());
              }))
              .scheduleOn(ex));
    });

    std::vector<folly::coro::Task<void>> tasks;
    tasks.push_back(std::move(task));
    co_await folly::coro::co_withCancellation(
        cancelSource.getToken(),
        folly::coro::collectAnyRange(std::move(tasks)));
    cancelSource.requestCancellation();
    baton.post();
    co_await scope.joinAsync();
  }());
}

class CollectAnyWithoutExceptionRangeTest : public testing::Test {};

TEST_F(CollectAnyWithoutExceptionRangeTest, OneTaskWithValue) {
  folly::coro::Baton baton;
  auto f = [&]() -> folly::coro::Task<std::string> {
    co_await baton;
    co_return "hello";
  };

  bool completed = false;
  auto run = [&]() -> folly::coro::Task<void> {
    std::vector<folly::coro::Task<std::string>> tasks;
    tasks.push_back(f());
    auto [index, result] =
        co_await folly::coro::collectAnyWithoutExceptionRange(std::move(tasks));
    EXPECT_EQ("hello", result.value());
    EXPECT_EQ(0, index);
    completed = true;
  };

  folly::ManualExecutor executor;

  auto future = run().scheduleOn(&executor).start();

  executor.drain();

  EXPECT_FALSE(completed);

  baton.post();

  // Posting the baton should have just scheduled the 'f()' coroutine
  // for resumption on the executor but should not have executed
  // until we drain the executor again.
  EXPECT_FALSE(completed);

  executor.drain();

  EXPECT_TRUE(completed);
  EXPECT_TRUE(future.isReady());
}

TEST_F(CollectAnyWithoutExceptionRangeTest, OneVoidTask) {
  bool completed = false;
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    // Checks that the task actually runs and that 'void' results are
    // promoted to folly::Unit when placed in a tuple.
    auto generateTasks =
        [&]() -> folly::coro::Generator<folly::coro::Task<void>&&> {
      co_yield [&]() -> folly::coro::Task<void> {
        completed = true;
        co_return;
      }();
    };
    std::pair<std::size_t, folly::Try<void>> result =
        co_await folly::coro::collectAnyWithoutExceptionRange(generateTasks());
    (void)result;
  }());
  EXPECT_TRUE(completed);
}

TEST_F(CollectAnyWithoutExceptionRangeTest, MoveOnlyType) {
  bool completed = false;
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    // Checks that the task actually runs and that move only results
    // can be correctly returned
    auto generateTasks = [&]()
        -> folly::coro::Generator<folly::coro::Task<std::unique_ptr<int>>&&> {
      co_yield [&]() -> folly::coro::Task<std::unique_ptr<int>> {
        completed = true;
        co_return std::make_unique<int>(1);
      }();
    };
    std::pair<std::size_t, folly::Try<std::unique_ptr<int>>> result =
        co_await folly::coro::collectAnyWithoutExceptionRange(generateTasks());
    (void)result;
  }());
  EXPECT_TRUE(completed);
}

TEST_F(
    CollectAnyWithoutExceptionRangeTest, DoesntCompleteUntilAllTasksComplete) {
  folly::coro::Baton baton1;
  folly::coro::Baton baton2;
  bool task1Started = false;
  bool task2Started = false;
  bool complete = false;
  constexpr std::size_t taskTerminatingExpectedIndex = 1;

  auto generateTasks =
      [&]() -> folly::coro::Generator<folly::coro::Task<int>&&> {
    auto t1 = [&]() -> folly::coro::Task<int> {
      task1Started = true;
      co_await baton1;
      co_return 42;
    };
    co_yield t1();
    auto t2 = [&]() -> folly::coro::Task<int> {
      task2Started = true;
      co_await baton2;
      co_return 314;
    };
    co_yield t2();
  };

  auto run = [&]() -> folly::coro::Task<void> {
    auto [index, result] =
        co_await folly::coro::collectAnyWithoutExceptionRange(generateTasks());
    complete = true;
    EXPECT_EQ(taskTerminatingExpectedIndex, index);
    EXPECT_EQ(result.value(), 314);
  };

  folly::ManualExecutor executor;

  auto future = run().scheduleOn(&executor).start();

  EXPECT_FALSE(task1Started);
  EXPECT_FALSE(task2Started);

  executor.drain();

  EXPECT_TRUE(task1Started);
  EXPECT_TRUE(task2Started);
  EXPECT_FALSE(complete);
  baton2.post();
  executor.drain();
  EXPECT_FALSE(complete);
  baton1.post();
  executor.drain();
  EXPECT_TRUE(complete);
  EXPECT_TRUE(future.isReady());
}

TEST_F(CollectAnyWithoutExceptionRangeTest, ThrowsLastError) {
  bool caughtException = false;

  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    bool throwError = true;

    auto generateTasks =
        [&]() -> folly::coro::Generator<folly::coro::Task<int>&&> {
      co_yield [&]() -> folly::coro::Task<int> {
        if (throwError) {
          throw ErrorA{};
        }
        co_return 1;
      }();
      co_yield [&]() -> folly::coro::Task<int> {
        if (throwError) {
          throw ErrorB{};
        }
        co_return 2;
      }();
      co_yield [&]() -> folly::coro::Task<int> {
        if (throwError) {
          throw ErrorC{};
        }
        co_return 3;
      }();
    };
    // Child tasks are started in-order.
    // Since the third task fails last we should see its exception
    // propagate out of collectAnyWithoutException().
    auto [index, result] =
        co_await folly::coro::collectAnyWithoutExceptionRange(generateTasks());
    EXPECT_EQ(2, index);
    try {
      result.value();
      ADD_FAILURE() << "Hit unexpected codepath";
    } catch (const ErrorC&) {
      caughtException = true;
    }
  }());
  EXPECT_TRUE(caughtException);
}

TEST_F(CollectAnyWithoutExceptionRangeTest, ReturnsFirstValueWithoutException) {
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    auto generateTasks =
        [&]() -> folly::coro::Generator<folly::coro::Task<int>&&> {
      co_yield []() -> folly::coro::Task<int> {
        throw ErrorA{};
        co_return 1;
      }();
      co_yield []() -> folly::coro::Task<int> {
        throw ErrorB{};
        co_return 2;
      }();
      co_yield []() -> folly::coro::Task<int> { co_return 3; }();
      co_yield []() -> folly::coro::Task<int> { co_return 4; }();
    };
    // Child tasks are started in-order.
    // Since the third task returns a value first ignore the exceptions and the
    // last value and propagate the third value out of
    // collectAnyWithoutException().
    auto [index, result] =
        co_await folly::coro::collectAnyWithoutExceptionRange(generateTasks());
    EXPECT_EQ(2, index);
    EXPECT_TRUE(result.hasValue());
    EXPECT_EQ(3, result.value());
  }());
}

TEST_F(
    CollectAnyWithoutExceptionRangeTest, CancelsSubtasksWhenASubtaskCompletes) {
  using namespace std::chrono_literals;

  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    auto start = std::chrono::steady_clock::now();
    std::vector<folly::coro::Task<int>> tasks;
    tasks.push_back([]() -> folly::coro::Task<int> {
      co_await sleepThatShouldBeCancelled(10s);
      co_return 42;
    }());
    tasks.push_back([]() -> folly::coro::Task<int> {
      co_await sleepThatShouldBeCancelled(5s);
      co_return 314;
    }());
    tasks.push_back([]() -> folly::coro::Task<int> {
      co_await folly::coro::co_reschedule_on_current_executor;
      co_return 1;
    }());

    auto [index, result] =
        co_await folly::coro::collectAnyWithoutExceptionRange(std::move(tasks));
    EXPECT_EQ(2, index);
    EXPECT_TRUE(result.hasValue());
    EXPECT_EQ(1, result.value());
    auto end = std::chrono::steady_clock::now();

    EXPECT_LT(end - start, 1s);
  }());
}

TEST_F(
    CollectAnyWithoutExceptionRangeTest,
    CancelsSubtasksWhenParentTaskCancelled) {
  using namespace std::chrono_literals;

  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    auto start = std::chrono::steady_clock::now();
    folly::CancellationSource cancelSource;
    auto generateTasks =
        [&]() -> folly::coro::Generator<folly::coro::Task<int>&&> {
      co_yield [&]() -> folly::coro::Task<int> {
        co_await sleepThatShouldBeCancelled(10s);
        co_return 42;
      }();
      co_yield [&]() -> folly::coro::Task<int> {
        co_await sleepThatShouldBeCancelled(5s);
        co_return 314;
      }();
      co_yield [&]() -> folly::coro::Task<int> {
        co_await folly::coro::co_reschedule_on_current_executor;
        co_await folly::coro::co_reschedule_on_current_executor;
        co_await folly::coro::co_reschedule_on_current_executor;
        cancelSource.requestCancellation();
        co_await sleepThatShouldBeCancelled(15s);
        co_return 123;
      }();
    };
    auto [index, result] = co_await folly::coro::co_withCancellation(
        cancelSource.getToken(),
        folly::coro::collectAnyWithoutExceptionRange(generateTasks()));
    auto end = std::chrono::steady_clock::now();
    EXPECT_LT(end - start, 1s);
  }());
}

TEST_F(
    CollectAnyWithoutExceptionRangeTest,
    CancellationTokenRemainsActiveAfterReturn) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    folly::CancellationSource cancelSource;
    folly::coro::AsyncScope scope;
    folly::coro::Baton baton;
    auto task = folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
      auto token = co_await folly::coro::co_current_cancellation_token;
      auto ex = co_await folly::coro::co_current_executor;
      scope.add(
          folly::coro::co_withCancellation(
              token, folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
                auto innerToken =
                    co_await folly::coro::co_current_cancellation_token;
                co_await baton;
                EXPECT_TRUE(innerToken.isCancellationRequested());
              }))
              .scheduleOn(ex));
    });

    std::vector<folly::coro::Task<void>> tasks;
    tasks.push_back(std::move(task));
    co_await folly::coro::co_withCancellation(
        cancelSource.getToken(),
        folly::coro::collectAnyWithoutExceptionRange(std::move(tasks)));
    cancelSource.requestCancellation();
    baton.post();
    co_await scope.joinAsync();
  }());
}

#endif

class CollectAnyNoDiscardRangeTest : public testing::Test {};

TEST_F(CollectAnyNoDiscardRangeTest, OneTask) {
  auto value = [&]() -> folly::coro::Task<std::string> { co_return "hello"; };
  auto throws = [&]() -> folly::coro::Task<std::string> {
    co_yield folly::coro::co_error(ErrorA{});
  };

  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    {
      std::vector<folly::coro::Task<std::string>> tasks;
      tasks.push_back(value());
      auto result =
          co_await folly::coro::collectAnyNoDiscardRange(std::move(tasks));
      EXPECT_EQ(result.size(), 1);
      EXPECT_TRUE(result[0].hasValue());
      EXPECT_EQ("hello", result[0].value());
    }
    {
      std::vector<folly::coro::Task<std::string>> tasks;
      tasks.push_back(throws());
      auto result =
          co_await folly::coro::collectAnyNoDiscardRange(std::move(tasks));
      EXPECT_EQ(result.size(), 1);
      EXPECT_TRUE(result[0].hasException<ErrorA>());
    }
  }());
}

TEST_F(CollectAnyNoDiscardRangeTest, MultipleTasksWithValues) {
  std::atomic_size_t count{0};

  // Busy wait until all threads have started before returning
  auto busyWait = [](std::atomic_size_t& count,
                     size_t num) -> folly::coro::Task<void> {
    count.fetch_add(1);
    while (count.load() < num) {
      // Need to yield because collectAnyNoDiscard() won't start the second and
      // third coroutines until the first one gets to a suspend point
      co_await folly::coro::co_reschedule_on_current_executor;
    }
  };

  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    std::vector<folly::coro::Task<void>> tasks;
    tasks.push_back(busyWait(count, 3));
    tasks.push_back(busyWait(count, 3));
    tasks.push_back(busyWait(count, 3));
    auto result =
        co_await folly::coro::collectAnyNoDiscardRange(std::move(tasks));
    EXPECT_EQ(result.size(), 3);
    EXPECT_TRUE(result[0].hasValue());
    EXPECT_TRUE(result[1].hasValue());
    EXPECT_TRUE(result[2].hasValue());
  }());
}

TEST_F(CollectAnyNoDiscardRangeTest, OneVoidTask) {
  bool completed = false;
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    // Checks that the task actually runs and that 'void' results are
    // promoted to folly::Unit when placed in a tuple.
    auto makeTask = [&]() -> folly::coro::Task<void> {
      completed = true;
      co_return;
    };
    std::vector<folly::coro::Task<void>> tasks;
    tasks.push_back(makeTask());
    auto result =
        co_await folly::coro::collectAnyNoDiscardRange(std::move(tasks));
    EXPECT_EQ(result.size(), 1);
  }());
  EXPECT_TRUE(completed);
}

TEST_F(CollectAnyNoDiscardRangeTest, DoesntCompleteUntilAllTasksComplete) {
  folly::coro::Baton baton1;
  folly::coro::Baton baton2;
  bool task1Started = false;
  bool task2Started = false;
  bool complete = false;

  auto run = [&]() -> folly::coro::Task<void> {
    auto generateTasks =
        [&]() -> folly::coro::Generator<folly::coro::Task<int>&&> {
      auto t1 = [&]() -> folly::coro::Task<int> {
        task1Started = true;
        co_await baton1;
        co_return 42;
      };
      co_yield t1();
      auto t2 = [&]() -> folly::coro::Task<int> {
        task2Started = true;
        co_await baton2;
        co_return 314;
      };
      co_yield t2();
    };

    auto result =
        co_await folly::coro::collectAnyNoDiscardRange(generateTasks());
    complete = true;
    EXPECT_EQ(result.size(), 2);
    EXPECT_TRUE(result[0].hasValue());
    EXPECT_EQ(result[0].value(), 42);
    EXPECT_TRUE(result[1].hasValue());
    EXPECT_EQ(result[1].value(), 314);
  };

  folly::ManualExecutor executor;

  auto future = run().scheduleOn(&executor).start();

  EXPECT_FALSE(task1Started);
  EXPECT_FALSE(task2Started);

  executor.drain();

  EXPECT_TRUE(task1Started);
  EXPECT_TRUE(task2Started);
  EXPECT_FALSE(complete);
  baton2.post();
  executor.drain();
  EXPECT_FALSE(complete);
  baton1.post();
  executor.drain();
  EXPECT_TRUE(complete);
  EXPECT_TRUE(future.isReady());
}

TEST_F(CollectAnyNoDiscardRangeTest, ThrowsAllErrors) {
  using namespace std::chrono_literals;

  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    // Child tasks are started in-order.
    // The first task will reschedule itself onto the executor.
    // The second task will fail immediately and will be the first task to fail.
    // Then the third and first tasks will fail.
    // We should see all exceptions propagate out of collectAnyNoDiscard().
    auto generateTasks =
        [&]() -> folly::coro::Generator<folly::coro::Task<int>&&> {
      co_yield [&]() -> folly::coro::Task<int> {
        co_await folly::coro::co_reschedule_on_current_executor;
        co_yield folly::coro::co_error(ErrorA{});
      }();
      co_yield [&]() -> folly::coro::Task<int> {
        co_yield folly::coro::co_error(ErrorB{});
      }();
      co_yield [&]() -> folly::coro::Task<int> {
        co_yield folly::coro::co_error(ErrorC{});
      }();
    };
    auto result =
        co_await folly::coro::collectAnyNoDiscardRange(generateTasks());
    EXPECT_EQ(result.size(), 3);
    EXPECT_TRUE(result[0].hasException<ErrorA>());
    EXPECT_TRUE(result[1].hasException<ErrorB>());
    EXPECT_TRUE(result[2].hasException<ErrorC>());
  }());
}

TEST_F(CollectAnyNoDiscardRangeTest, CancelSubtasksWhenASubtaskCompletes) {
  using namespace std::chrono_literals;

  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    auto start = std::chrono::steady_clock::now();

    auto generateTasks =
        [&]() -> folly::coro::Generator<folly::coro::Task<int>&&> {
      co_yield []() -> folly::coro::Task<int> {
        co_await folly::coro::sleep(10s);
        co_return 42;
      }();
      co_yield []() -> folly::coro::Task<int> {
        co_await folly::coro::sleep(5s);
        co_return 314;
      }();
      co_yield []() -> folly::coro::Task<int> {
        co_await folly::coro::co_reschedule_on_current_executor;
        co_yield folly::coro::co_error(ErrorA{});
      }();
    };
    auto result =
        co_await folly::coro::collectAnyNoDiscardRange(generateTasks());
    EXPECT_EQ(result.size(), 3);
    EXPECT_TRUE(result[0].hasException<folly::OperationCancelled>());
    EXPECT_TRUE(result[1].hasException<folly::OperationCancelled>());
    EXPECT_TRUE(result[2].hasException<ErrorA>());

    auto end = std::chrono::steady_clock::now();

    EXPECT_LT(end - start, 1s);
  }());
}

TEST_F(CollectAnyNoDiscardRangeTest, CancelSubtasksWhenParentTaskCancelled) {
  using namespace std::chrono_literals;

  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    auto start = std::chrono::steady_clock::now();
    folly::CancellationSource cancelSource;

    auto generateTasks =
        [&]() -> folly::coro::Generator<folly::coro::Task<int>&&> {
      co_yield [&]() -> folly::coro::Task<int> {
        co_await folly::coro::sleep(10s);
        co_return 42;
      }();
      co_yield [&]() -> folly::coro::Task<int> {
        co_await folly::coro::sleep(5s);
        co_return 314;
      }();
      co_yield [&]() -> folly::coro::Task<int> {
        co_await folly::coro::co_reschedule_on_current_executor;
        co_await folly::coro::co_reschedule_on_current_executor;
        co_await folly::coro::co_reschedule_on_current_executor;
        cancelSource.requestCancellation();
        co_await folly::coro::sleep(15s);
        co_return 123;
      }();
    };
    auto result = co_await folly::coro::co_withCancellation(
        cancelSource.getToken(),
        folly::coro::collectAnyNoDiscardRange(generateTasks()));
    auto end = std::chrono::steady_clock::now();
    EXPECT_LT(end - start, 1s);
    EXPECT_EQ(result.size(), 3);
    EXPECT_TRUE(result[0].hasException<folly::OperationCancelled>());
    EXPECT_TRUE(result[1].hasException<folly::OperationCancelled>());
    EXPECT_TRUE(result[2].hasException<folly::OperationCancelled>());
  }());
}

TEST_F(
    CollectAnyNoDiscardRangeTest, CancellationTokenRemainsActiveAfterReturn) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    folly::CancellationSource cancelSource;
    folly::coro::AsyncScope scope;
    folly::coro::Baton baton;
    std::vector<folly::coro::Task<void>> tasks;
    tasks.push_back(folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
      auto token = co_await folly::coro::co_current_cancellation_token;
      auto ex = co_await folly::coro::co_current_executor;
      scope.add(
          folly::coro::co_withCancellation(
              token, folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
                auto innerToken =
                    co_await folly::coro::co_current_cancellation_token;
                co_await baton;
                EXPECT_TRUE(innerToken.isCancellationRequested());
              }))
              .scheduleOn(ex));
    }));

    co_await folly::coro::co_withCancellation(
        cancelSource.getToken(),
        folly::coro::collectAnyNoDiscardRange(std::move(tasks)));
    cancelSource.requestCancellation();
    baton.post();
    co_await scope.joinAsync();
  }());
}
