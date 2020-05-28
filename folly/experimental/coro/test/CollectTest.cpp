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

#include <folly/Portability.h>

#if FOLLY_HAS_COROUTINES

#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/executors/ManualExecutor.h>
#include <folly/experimental/coro/Baton.h>
#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/Collect.h>
#include <folly/experimental/coro/CurrentExecutor.h>
#include <folly/experimental/coro/Generator.h>
#include <folly/experimental/coro/Mutex.h>
#include <folly/experimental/coro/Sleep.h>
#include <folly/experimental/coro/Task.h>
#include <folly/io/async/Request.h>
#include <folly/portability/GTest.h>

#include <numeric>
#include <string>
#include <vector>

class CollectAllTest : public testing::Test {};

TEST_F(CollectAllTest, WithNoArgs) {
  bool completed = false;
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    std::tuple<> result = co_await folly::coro::collectAll();
    completed = true;
    (void)result;
  }());
  CHECK(completed);
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
  CHECK(completed);
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
      CHECK(false);
    } catch (const ErrorB&) {
      caughtException = true;
    }
  }());
  CHECK(caughtException);
}

TEST_F(CollectAllTest, SynchronousCompletionInLoopDoesntCauseStackOverflow) {
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

struct OperationCancelled : std::exception {};

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

        CHECK_EQ(999'989, result);
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
      CHECK(false);
      (void)a;
      (void)b;
      (void)c;
    } catch (const ErrorA&) {
    }
    auto end = std::chrono::steady_clock::now();

    CHECK((end - start) < 1s);
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
              co_await folly::coro::sleep(10s);
              co_return 42;
            }(),
            [&]() -> folly::coro::Task<float> {
              co_await folly::coro::sleep(5s);
              co_return 3.14f;
            }(),
            [&]() -> folly::coro::Task<void> {
              co_await folly::coro::co_reschedule_on_current_executor;
              co_await folly::coro::co_reschedule_on_current_executor;
              co_await folly::coro::co_reschedule_on_current_executor;
              cancelSource.requestCancellation();
            }()));
    auto end = std::chrono::steady_clock::now();

    CHECK((end - start) < 1s);
    CHECK_EQ(42, a);
    CHECK_EQ(3.14f, b);
    (void)c;
  }());
}

namespace {

class TestRequestData : public folly::RequestData {
 public:
  explicit TestRequestData() noexcept {}

  bool hasCallback() override {
    return false;
  }
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
      CHECK(getContextData() == initialContextData);
      folly::RequestContextScopeGuard childScope;
      CHECK(getContextData() == nullptr);
      co_await folly::coro::co_reschedule_on_current_executor;
      CHECK(getContextData() == nullptr);
      setContextData();
      auto newContextData = getContextData();
      CHECK(newContextData != nullptr);
      CHECK(newContextData != initialContextData);
      co_await folly::coro::co_reschedule_on_current_executor;
      CHECK(getContextData() == newContextData);
    };

    co_await folly::coro::collectAll(makeChildTask(), makeChildTask());

    CHECK(getContextData() == initialContextData);
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
    CHECK(a == 42);
    CHECK(b == "coroutine");
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
  CHECK(completed);
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

TEST_F(CollectAllTryTest, OneTaskWithError) {
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
    CHECK(cRes.hasException());
    CHECK(cRes.exception().get_exception<ErrorB>() != nullptr);
    CHECK(dRes.hasValue());
    CHECK_EQ(3.1415, dRes.value());
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
          CHECK(!(co_await folly::coro::co_current_cancellation_token)
                     .isCancellationRequested());
          co_return 42;
        }(),
        []() -> folly::coro::Task<float> {
          co_await folly::coro::co_reschedule_on_current_executor;
          co_await folly::coro::co_reschedule_on_current_executor;
          co_await folly::coro::co_reschedule_on_current_executor;
          co_await folly::coro::co_reschedule_on_current_executor;
          co_await folly::coro::co_reschedule_on_current_executor;
          CHECK(!(co_await folly::coro::co_current_cancellation_token)
                     .isCancellationRequested());
          co_return 3.14f;
        }(),
        []() -> folly::coro::Task<void> {
          co_await folly::coro::co_reschedule_on_current_executor;
          throw ErrorA{};
        }());

    CHECK(a.hasValue());
    CHECK_EQ(42, a.value());
    CHECK(b.hasValue());
    CHECK_EQ(3.14f, b.value());
    CHECK(c.hasException());
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
              co_await folly::coro::sleep(10s);
              co_return 42;
            }(),
            [&]() -> folly::coro::Task<float> {
              co_await folly::coro::sleep(5s);
              co_return 3.14f;
            }(),
            [&]() -> folly::coro::Task<void> {
              co_await folly::coro::co_reschedule_on_current_executor;
              co_await folly::coro::co_reschedule_on_current_executor;
              co_await folly::coro::co_reschedule_on_current_executor;
              cancelSource.requestCancellation();
            }()));
    auto end = std::chrono::steady_clock::now();

    CHECK((end - start) < 1s);
    CHECK_EQ(42, a.value());
    CHECK_EQ(3.14f, b.value());
    CHECK(c.hasValue());
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
      CHECK(getContextData() == initialContextData);
      folly::RequestContextScopeGuard childScope;
      CHECK(getContextData() == nullptr);
      co_await folly::coro::co_reschedule_on_current_executor;
      CHECK(getContextData() == nullptr);
      setContextData();
      auto newContextData = getContextData();
      CHECK(newContextData != nullptr);
      CHECK(newContextData != initialContextData);
      co_await folly::coro::co_reschedule_on_current_executor;
      CHECK(getContextData() == newContextData);
    };

    co_await folly::coro::collectAllTry(makeChildTask(), makeChildTask());

    CHECK(getContextData() == initialContextData);
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

    CHECK_EQ(3, count);
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
      CHECK(false);
    } catch (const ErrorA&) {
    }

    CHECK_EQ(5, count);
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

    CHECK_EQ(0, count);

    std::vector<int> results =
        co_await folly::coro::collectAllRange(std::move(tasks));

    CHECK_EQ(taskCount, results.size());
    CHECK_EQ(taskCount, count);

    for (int i = 0; i < taskCount; ++i) {
      CHECK_EQ(i, results[i]);
    }
  }());
}

TEST_F(CollectAllRangeTest, SubtasksCancelledWhenASubtaskFails) {
  using namespace std::chrono_literals;

  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    bool consumedAllTasks = false;
    auto generateTasks = [&]()
        -> folly::coro::Generator<folly::coro::Task<void>&&> {
      for (int i = 0; i < 10; ++i) {
        co_yield folly::coro::sleep(10s);
      }

      co_yield[]()->folly::coro::Task<void> {
        co_await folly::coro::co_reschedule_on_current_executor;
        throw ErrorA{};
      }
      ();

      for (int i = 0; i < 10; ++i) {
        co_yield folly::coro::sleep(10s);
      }

      consumedAllTasks = true;
    };

    auto start = std::chrono::steady_clock::now();
    try {
      co_await folly::coro::collectAllRange(generateTasks());
      CHECK(false);
    } catch (const ErrorA&) {
    }
    auto end = std::chrono::steady_clock::now();

    CHECK((end - start) < 1s);
    CHECK(consumedAllTasks);
  }());
}

TEST_F(CollectAllRangeTest, FailsWithErrorOfFirstTaskToFailWhenMultipleErrors) {
  using namespace std::chrono_literals;

  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    try {
      co_await folly::coro::collectAllRange(
          []() -> folly::coro::Generator<folly::coro::Task<void>&&> {
            co_yield folly::coro::sleep(1s);
            co_yield[]()->folly::coro::Task<> {
              co_await folly::coro::sleep(1s);
              throw ErrorA{};
            }
            ();
            co_yield[]()->folly::coro::Task<> {
              co_await folly::coro::co_reschedule_on_current_executor;
              throw ErrorB{};
            }
            ();
            co_yield folly::coro::sleep(2s);
          }());
      CHECK(false);
    } catch (const ErrorB&) {
    }
  }());
}

TEST_F(CollectAllRangeTest, SubtasksCancelledWhenParentTaskCancelled) {
  using namespace std::chrono_literals;

  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    folly::CancellationSource cancelSource;

    bool consumedAllTasks = false;
    auto generateTasks = [&]()
        -> folly::coro::Generator<folly::coro::Task<void>&&> {
      for (int i = 0; i < 10; ++i) {
        co_yield folly::coro::sleep(10s);
      }

      co_yield[&]()->folly::coro::Task<void> {
        co_await folly::coro::co_reschedule_on_current_executor;
        co_await folly::coro::co_reschedule_on_current_executor;
        co_await folly::coro::co_reschedule_on_current_executor;
        co_await folly::coro::co_reschedule_on_current_executor;
        cancelSource.requestCancellation();

        auto token = co_await folly::coro::co_current_cancellation_token;
        CHECK(token.isCancellationRequested());
      }
      ();

      consumedAllTasks = true;
    };

    auto start = std::chrono::steady_clock::now();
    co_await folly::coro::co_withCancellation(
        cancelSource.getToken(), folly::coro::collectAllRange(generateTasks()));
    auto end = std::chrono::steady_clock::now();

    CHECK((end - start) < 1s);
    CHECK(consumedAllTasks);
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
      CHECK(getContextData() == initialContextData);
      folly::RequestContextScopeGuard childScope;
      CHECK(getContextData() == nullptr);
      co_await folly::coro::co_reschedule_on_current_executor;
      CHECK(getContextData() == nullptr);
      setContextData();
      auto newContextData = getContextData();
      CHECK(newContextData != nullptr);
      CHECK(newContextData != initialContextData);
      co_await folly::coro::co_reschedule_on_current_executor;
      CHECK(getContextData() == newContextData);
    };

    std::vector<folly::coro::Task<void>> tasks;
    tasks.emplace_back(makeChildTask());
    tasks.emplace_back(makeChildTask());
    tasks.emplace_back(makeChildTask());

    co_await folly::coro::collectAllRange(std::move(tasks));

    CHECK(getContextData() == initialContextData);
  }());
}

TEST_F(CollectAllRangeTest, VectorOfTaskWithExecutorUsage) {
  folly::CPUThreadPoolExecutor threadPool{
      4, std::make_shared<folly::NamedThreadFactory>("TestThreadPool")};

  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    std::vector<folly::coro::TaskWithExecutor<int>> tasks;
    for (int i = 0; i < 4; ++i) {
      tasks.push_back(
          [](int i) -> folly::coro::Task<int> { co_return i + 1; }(i)
                           .scheduleOn(&threadPool));
    }

    auto results = co_await folly::coro::collectAllRange(std::move(tasks));
    CHECK(results.size() == 4);
    CHECK(results[0] == 1);
    CHECK(results[1] == 2);
    CHECK(results[2] == 3);
    CHECK(results[3] == 4);
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

    CHECK_EQ(5, results.size());
    CHECK(results[0].hasValue());
    CHECK(results[1].hasValue());
    CHECK(results[2].hasException());
    CHECK(results[3].hasValue());
    CHECK(results[4].hasValue());
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

    CHECK_EQ(6, results.size());
    CHECK(results[0].hasValue());
    CHECK_EQ("testing", results[0].value());
    CHECK(results[1].hasValue());
    CHECK_EQ("testing", results[1].value());
    CHECK(results[2].hasException());
    CHECK(results[3].hasValue());
    CHECK_EQ("testing", results[3].value());
    CHECK(results[4].hasValue());
    CHECK_EQ("testing", results[4].value());
    CHECK(results[5].hasException());
  }());
}

TEST_F(CollectAllTryRangeTest, NotCancelledWhenSubtaskFails) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    auto generateTasks = [&]()
        -> folly::coro::Generator<folly::coro::Task<void>&&> {
      auto makeValidationTask = []() -> folly::coro::Task<void> {
        co_await folly::coro::co_reschedule_on_current_executor;
        co_await folly::coro::co_reschedule_on_current_executor;
        co_await folly::coro::co_reschedule_on_current_executor;
        auto token = co_await folly::coro::co_current_cancellation_token;
        CHECK(!token.isCancellationRequested());
      };

      co_yield makeValidationTask();
      co_yield makeValidationTask();

      co_yield[]()->folly::coro::Task<void> {
        co_await folly::coro::co_reschedule_on_current_executor;
        throw ErrorA{};
      }
      ();

      co_yield makeValidationTask();
      co_yield makeValidationTask();
    };

    auto results = co_await folly::coro::collectAllTryRange(generateTasks());
    CHECK_EQ(5, results.size());
    CHECK(results[0].hasValue());
    CHECK(results[1].hasValue());
    CHECK(results[2].hasException());
    CHECK(results[3].hasValue());
    CHECK(results[4].hasValue());
  }());
}

TEST_F(CollectAllTryRangeTest, SubtasksCancelledWhenParentTaskCancelled) {
  using namespace std::chrono_literals;

  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    folly::CancellationSource cancelSource;

    bool consumedAllTasks = false;
    auto generateTasks = [&]()
        -> folly::coro::Generator<folly::coro::Task<void>&&> {
      for (int i = 0; i < 10; ++i) {
        co_yield folly::coro::sleep(10s);
      }

      co_yield[&]()->folly::coro::Task<void> {
        co_await folly::coro::co_reschedule_on_current_executor;
        co_await folly::coro::co_reschedule_on_current_executor;
        co_await folly::coro::co_reschedule_on_current_executor;
        co_await folly::coro::co_reschedule_on_current_executor;
        cancelSource.requestCancellation();

        auto token = co_await folly::coro::co_current_cancellation_token;
        CHECK(token.isCancellationRequested());
      }
      ();

      consumedAllTasks = true;
    };

    auto start = std::chrono::steady_clock::now();
    auto results = co_await folly::coro::co_withCancellation(
        cancelSource.getToken(),
        folly::coro::collectAllTryRange(generateTasks()));
    auto end = std::chrono::steady_clock::now();

    CHECK_EQ(11, results.size());
    for (auto& result : results) {
      CHECK(result.hasValue());
    }
    CHECK((end - start) < 1s);
    CHECK(consumedAllTasks);
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
      CHECK(getContextData() == initialContextData);
      folly::RequestContextScopeGuard childScope;
      CHECK(getContextData() == nullptr);
      co_await folly::coro::co_reschedule_on_current_executor;
      CHECK(getContextData() == nullptr);
      setContextData();
      auto newContextData = getContextData();
      CHECK(newContextData != nullptr);
      CHECK(newContextData != initialContextData);
      co_await folly::coro::co_reschedule_on_current_executor;
      CHECK(getContextData() == newContextData);
    };

    std::vector<folly::coro::Task<void>> tasks;
    tasks.emplace_back(makeChildTask());
    tasks.emplace_back(makeChildTask());
    tasks.emplace_back(makeChildTask());

    co_await folly::coro::collectAllTryRange(std::move(tasks));

    CHECK(getContextData() == initialContextData);
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
          co_yield[](int i)->Task<std::string> {
            co_await folly::coro::co_reschedule_on_current_executor;
            co_return folly::to<std::string>(i);
          }
          (i);
        }
      }(),
      10));

  CHECK_EQ(10'000, results.size());
  for (int i = 0; i < 10'000; ++i) {
    CHECK_EQ(folly::to<std::string>(i), results[i]);
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

  auto makeTaskGenerator = [&]()
      -> folly::coro::Generator<folly::coro::Task<int>&&> {
    for (int i = 0; i < 100; ++i) {
      co_yield makeTask(i);
    }
  };

  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    auto results = co_await folly::coro::collectAllWindowed(
        makeTaskGenerator(), maxConcurrency);
    CHECK_EQ(100, results.size());
    for (int i = 0; i < 100; ++i) {
      CHECK_EQ(i, results[i]);
    }
  }());

  CHECK_EQ(0, activeCount.load());
  CHECK_EQ(100, completedCount);
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

  auto makeTaskGenerator = [&]()
      -> folly::coro::Generator<folly::coro::Task<void>&&> {
    for (int i = 0; i < 100; ++i) {
      co_yield makeTask();
    }
  };

  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    co_await folly::coro::collectAllWindowed(
        makeTaskGenerator(), maxConcurrency);
  }());

  CHECK_EQ(0, activeCount.load());
  CHECK_EQ(100, completedCount);
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

  CHECK_EQ(10, count);
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

  CHECK_EQ(10, count);
  CHECK_EQ(10, results.size());
  for (int i = 0; i < 10; ++i) {
    CHECK_EQ(i, *results[i]);
  }
}

TEST_F(CollectAllWindowedTest, MultipleFailuresPropagatesFirstError) {
  try {
    [[maybe_unused]] auto results =
        folly::coro::blockingWait(folly::coro::collectAllWindowed(
            []() -> folly::coro::Generator<folly::coro::Task<int>&&> {
              for (int i = 0; i < 10; ++i) {
                co_yield[](int i)->folly::coro::Task<int> {
                  using namespace std::literals::chrono_literals;
                  if (i == 3) {
                    co_await folly::coro::co_reschedule_on_current_executor;
                    co_await folly::coro::co_reschedule_on_current_executor;
                    throw ErrorA{};
                  } else if (i == 7) {
                    co_await folly::coro::co_reschedule_on_current_executor;
                    throw ErrorB{};
                  }
                  co_return i;
                }
                (i);
              }
            }(),
            5));
    CHECK(false); // Should have thrown.
  } catch (ErrorB) {
    // Expected.
  }
}

TEST_F(CollectAllWindowedTest, SubtasksCancelledWhenASubtaskFails) {
  using namespace std::chrono_literals;

  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    bool consumedAllTasks = false;
    auto generateTasks = [&]()
        -> folly::coro::Generator<folly::coro::Task<void>&&> {
      co_yield[]()->folly::coro::Task<void> {
        co_await folly::coro::co_reschedule_on_current_executor;
        throw ErrorA{};
      }
      ();

      for (int i = 0; i < 10; ++i) {
        co_yield folly::coro::sleep(10s);
      }

      consumedAllTasks = true;
    };

    auto start = std::chrono::steady_clock::now();
    try {
      co_await folly::coro::collectAllWindowed(generateTasks(), 2);
      CHECK(false);
    } catch (const ErrorA&) {
    }
    auto end = std::chrono::steady_clock::now();

    CHECK((end - start) < 1s);
    CHECK(consumedAllTasks);
  }());
}

TEST_F(CollectAllWindowedTest, SubtasksCancelledWhenParentTaskCancelled) {
  using namespace std::chrono_literals;

  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    folly::CancellationSource cancelSource;

    bool consumedAllTasks = false;
    auto generateTasks = [&]()
        -> folly::coro::Generator<folly::coro::Task<void>&&> {
      co_yield folly::coro::sleep(10s);
      co_yield folly::coro::sleep(10s);

      co_yield[&]()->folly::coro::Task<void> {
        co_await folly::coro::co_reschedule_on_current_executor;
        co_await folly::coro::co_reschedule_on_current_executor;
        co_await folly::coro::co_reschedule_on_current_executor;
        co_await folly::coro::co_reschedule_on_current_executor;
        cancelSource.requestCancellation();

        auto token = co_await folly::coro::co_current_cancellation_token;
        CHECK(token.isCancellationRequested());
      }
      ();

      co_yield folly::coro::sleep(10s);
      co_yield folly::coro::sleep(10s);

      consumedAllTasks = true;
    };

    auto start = std::chrono::steady_clock::now();
    co_await folly::coro::co_withCancellation(
        cancelSource.getToken(),
        folly::coro::collectAllWindowed(generateTasks(), 4));
    auto end = std::chrono::steady_clock::now();

    CHECK((end - start) < 1s);
    CHECK(consumedAllTasks);
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
      CHECK(getContextData() == initialContextData);
      folly::RequestContextScopeGuard childScope;
      CHECK(getContextData() == nullptr);
      co_await folly::coro::co_reschedule_on_current_executor;
      CHECK(getContextData() == nullptr);
      setContextData();
      auto newContextData = getContextData();
      CHECK(newContextData != nullptr);
      CHECK(newContextData != initialContextData);
      co_await folly::coro::co_reschedule_on_current_executor;
      CHECK(getContextData() == newContextData);
    };

    std::vector<folly::coro::Task<void>> tasks;
    tasks.emplace_back(makeChildTask());
    tasks.emplace_back(makeChildTask());
    tasks.emplace_back(makeChildTask());

    co_await folly::coro::collectAllWindowed(std::move(tasks), 2);

    CHECK(getContextData() == initialContextData);
  }());
}

TEST_F(CollectAllWindowedTest, VectorOfTaskWithExecutorUsage) {
  folly::CPUThreadPoolExecutor threadPool{
      4, std::make_shared<folly::NamedThreadFactory>("TestThreadPool")};

  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    std::vector<folly::coro::TaskWithExecutor<int>> tasks;
    for (int i = 0; i < 4; ++i) {
      tasks.push_back(
          [](int i) -> folly::coro::Task<int> { co_return i + 1; }(i)
                           .scheduleOn(&threadPool));
    }

    auto results =
        co_await folly::coro::collectAllWindowed(std::move(tasks), 2);
    CHECK(results.size() == 4);
    CHECK(results[0] == 1);
    CHECK(results[1] == 2);
    CHECK(results[2] == 3);
    CHECK(results[3] == 4);
  }());
}

class CollectAllTryWindowedTest : public testing::Test {};

TEST_F(CollectAllTryWindowedTest, PartialFailure) {
  auto results = folly::coro::blockingWait(folly::coro::collectAllTryWindowed(
      []() -> folly::coro::Generator<folly::coro::Task<int>&&> {
        for (int i = 0; i < 10; ++i) {
          co_yield[](int i)->folly::coro::Task<int> {
            using namespace std::literals::chrono_literals;
            if (i == 3) {
              co_await folly::coro::co_reschedule_on_current_executor;
              co_await folly::coro::co_reschedule_on_current_executor;
              throw ErrorA{};
            } else if (i == 7) {
              co_await folly::coro::co_reschedule_on_current_executor;
              throw ErrorB{};
            }
            co_return i;
          }
          (i);
        }
      }(),
      5));
  CHECK_EQ(10, results.size());

  for (int i = 0; i < 10; ++i) {
    if (i == 3) {
      CHECK(results[i].hasException());
      CHECK(results[i].exception().is_compatible_with<ErrorA>());
    } else if (i == 7) {
      CHECK(results[i].hasException());
      CHECK(results[i].exception().is_compatible_with<ErrorB>());
    } else {
      CHECK(results[i].hasValue());
      CHECK_EQ(i, results[i].value());
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

  auto generateTasks = [&]()
      -> folly::coro::Generator<folly::coro::Task<void>&&> {
    for (int i = 0; i < 10; ++i) {
      co_yield makeTask(i);
    }
    throw ErrorA{};
  };

  try {
    [[maybe_unused]] auto results = folly::coro::blockingWait(
        folly::coro::collectAllTryWindowed(generateTasks(), 5));
    CHECK(false);
  } catch (ErrorA) {
  }

  // Even if the generator throws an exception we should still have launched
  // and waited for completion all of the prior tasks in the sequence.
  CHECK_EQ(10, started);
  CHECK_EQ(0, active);
}

TEST_F(CollectAllTryWindowedTest, NotCancelledWhenSubtaskFails) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    auto generateTasks = [&]()
        -> folly::coro::Generator<folly::coro::Task<void>&&> {
      co_yield[]()->folly::coro::Task<void> {
        co_await folly::coro::co_reschedule_on_current_executor;
        throw ErrorA{};
      }
      ();

      auto makeValidationTask = []() -> folly::coro::Task<void> {
        co_await folly::coro::co_reschedule_on_current_executor;
        co_await folly::coro::co_reschedule_on_current_executor;
        co_await folly::coro::co_reschedule_on_current_executor;
        auto token = co_await folly::coro::co_current_cancellation_token;
        CHECK(!token.isCancellationRequested());
      };

      co_yield makeValidationTask();
      co_yield makeValidationTask();
    };

    auto results =
        co_await folly::coro::collectAllTryWindowed(generateTasks(), 2);
    CHECK_EQ(3, results.size());
    CHECK(results[0].hasException());
    CHECK(results[1].hasValue());
    CHECK(results[2].hasValue());
  }());
}

TEST_F(CollectAllTryWindowedTest, SubtasksCancelledWhenParentTaskCancelled) {
  using namespace std::chrono_literals;

  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    folly::CancellationSource cancelSource;

    bool consumedAllTasks = false;
    auto generateTasks = [&]()
        -> folly::coro::Generator<folly::coro::Task<void>&&> {
      co_yield folly::coro::sleep(10s);
      co_yield folly::coro::sleep(10s);

      co_yield[&]()->folly::coro::Task<void> {
        co_await folly::coro::co_reschedule_on_current_executor;
        co_await folly::coro::co_reschedule_on_current_executor;
        co_await folly::coro::co_reschedule_on_current_executor;
        co_await folly::coro::co_reschedule_on_current_executor;
        cancelSource.requestCancellation();

        auto token = co_await folly::coro::co_current_cancellation_token;
        CHECK(token.isCancellationRequested());
      }
      ();

      co_yield folly::coro::sleep(10s);
      co_yield folly::coro::sleep(10s);

      consumedAllTasks = true;
    };

    auto start = std::chrono::steady_clock::now();
    auto results = co_await folly::coro::co_withCancellation(
        cancelSource.getToken(),
        folly::coro::collectAllTryWindowed(generateTasks(), 4));
    auto end = std::chrono::steady_clock::now();

    CHECK_EQ(5, results.size());
    for (auto& result : results) {
      CHECK(result.hasValue());
    }
    CHECK((end - start) < 1s);
    CHECK(consumedAllTasks);
  }());
}

#endif
