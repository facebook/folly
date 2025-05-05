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

#include <folly/executors/InlineExecutor.h>
#include <folly/executors/ManualExecutor.h>
#include <folly/experimental/coro/Baton.h>
#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/Invoke.h>
#include <folly/experimental/coro/Mutex.h>
#include <folly/experimental/coro/SharedMutex.h>
#include <folly/experimental/coro/Task.h>
#include <folly/experimental/coro/detail/InlineTask.h>
#include <folly/futures/Future.h>
#include <folly/io/async/ScopedEventBaseThread.h>
#include <folly/portability/GTest.h>

#include <type_traits>

#if FOLLY_HAS_COROUTINES

using namespace folly;

static_assert(
    std::is_same<
        folly::coro::semi_await_result_t<folly::coro::Task<void>>,
        void>::value,
    "");
static_assert(
    std::is_same<
        folly::coro::semi_await_result_t<folly::coro::Task<int>>,
        int>::value,
    "");

static_assert(
    std::is_same<
        folly::coro::semi_await_result_t<folly::coro::detail::InlineTask<void>>,
        void>::value,
    "");
static_assert(
    std::is_same<
        folly::coro::semi_await_result_t<folly::coro::detail::InlineTask<int>>,
        int>::value,
    "");

static_assert(
    std::is_same<folly::coro::semi_await_result_t<folly::coro::Baton&>, void>::
        value,
    "");
static_assert(
    std::is_same<
        folly::coro::semi_await_result_t<
            decltype(std::declval<folly::coro::SharedMutex&>()
                         .co_scoped_lock_shared())>,
        folly::coro::SharedLock<folly::coro::SharedMutex>>::value,
    "");

namespace {

const RequestToken testToken1("corotest1");
const RequestToken testToken2("corotest2");

class TestRequestData : public RequestData {
 public:
  explicit TestRequestData(std::string key) noexcept : key_(std::move(key)) {}

  bool hasCallback() override { return false; }

  const std::string& key() const noexcept { return key_; }

 private:
  std::string key_;
};

} // namespace

static coro::Task<void> childRequest(coro::Mutex& m, coro::Baton& b) {
  ShallowCopyRequestContextScopeGuard requestScope;

  auto* parentContext = dynamic_cast<TestRequestData*>(
      RequestContext::get()->getContextData(testToken1));

  EXPECT_TRUE(parentContext != nullptr);

  auto childKey = parentContext->key() + ".child";

  RequestContext::get()->setContextData(
      testToken2, std::make_unique<TestRequestData>(childKey));

  auto* childContext = dynamic_cast<TestRequestData*>(
      RequestContext::get()->getContextData(testToken2));
  CHECK(childContext != nullptr);

  {
    auto lock = co_await m.co_scoped_lock();

    CHECK_EQ(
        parentContext,
        dynamic_cast<TestRequestData*>(
            RequestContext::get()->getContextData(testToken1)));
    CHECK_EQ(
        childContext,
        dynamic_cast<TestRequestData*>(
            RequestContext::get()->getContextData(testToken2)));

    co_await b;

    CHECK_EQ(
        parentContext,
        dynamic_cast<TestRequestData*>(
            RequestContext::get()->getContextData(testToken1)));
    CHECK_EQ(
        childContext,
        dynamic_cast<TestRequestData*>(
            RequestContext::get()->getContextData(testToken2)));
  }

  CHECK_EQ(
      parentContext,
      dynamic_cast<TestRequestData*>(
          RequestContext::get()->getContextData(testToken1)));
  CHECK_EQ(
      childContext,
      dynamic_cast<TestRequestData*>(
          RequestContext::get()->getContextData(testToken2)));
}

static coro::Task<void> parentRequest(int id) {
  ShallowCopyRequestContextScopeGuard requestScope;

  // Should have captured the value at the time the coroutine was co_awaited
  // rather than at the time the coroutine was called.
  auto* globalData = dynamic_cast<TestRequestData*>(
      RequestContext::get()->getContextData("global"));
  CHECK(globalData != nullptr);
  CHECK_EQ("other value", globalData->key());

  std::string key = folly::to<std::string>("request", id);

  RequestContext::get()->setContextData(
      testToken1, std::make_unique<TestRequestData>(key));

  auto* contextData = RequestContext::get()->getContextData(testToken1);
  CHECK(contextData != nullptr);

  coro::Mutex mutex;
  coro::Baton baton1;
  coro::Baton baton2;

  auto fut1 = childRequest(mutex, baton1)
                  .scheduleOn(co_await coro::co_current_executor)
                  .start();
  auto fut2 = childRequest(mutex, baton1)
                  .scheduleOn(co_await coro::co_current_executor)
                  .start();

  CHECK_EQ(contextData, RequestContext::get()->getContextData(testToken1));

  baton1.post();
  baton2.post();

  (void)co_await std::move(fut1);

  CHECK_EQ(contextData, RequestContext::get()->getContextData(testToken1));

  // Check that context from child operation doesn't leak into this coroutine.
  CHECK(RequestContext::get()->getContextData(testToken2) == nullptr);

  (void)co_await std::move(fut2);

  // Check that context from child operation doesn't leak into this coroutine.
  CHECK(RequestContext::get()->getContextData(testToken2) == nullptr);
}

class TaskTest : public testing::Test {};

TEST_F(TaskTest, RequestContextIsPreservedAcrossSuspendResume) {
  ManualExecutor executor;

  RequestContextScopeGuard requestScope;

  RequestContext::get()->setContextData(
      "global", std::make_unique<TestRequestData>("global value"));

  // Context should be captured at coroutine co_await time and not at
  // call time.
  auto task1 = parentRequest(1).scheduleOn(&executor);
  auto task2 = parentRequest(2).scheduleOn(&executor);

  {
    RequestContextScopeGuard nestedRequestScope;

    RequestContext::get()->setContextData(
        "global", std::make_unique<TestRequestData>("other value"));

    // Start execution of the tasks.
    auto fut1 = std::move(task1).start();
    auto fut2 = std::move(task2).start();

    // Check that the contexts set by starting the tasks don't bleed out
    // to the caller.
    CHECK(RequestContext::get()->getContextData(testToken1) == nullptr);
    CHECK(RequestContext::get()->getContextData(testToken2) == nullptr);
    CHECK_EQ(
        "other value",
        dynamic_cast<TestRequestData*>(
            RequestContext::get()->getContextData("global"))
            ->key());

    executor.drain();

    CHECK(fut1.isReady());
    CHECK(fut2.isReady());

    // Check that the contexts set by the coroutines executing on the executor
    // do not leak out to the caller.
    CHECK(RequestContext::get()->getContextData(testToken1) == nullptr);
    CHECK(RequestContext::get()->getContextData(testToken2) == nullptr);
    CHECK_EQ(
        "other value",
        dynamic_cast<TestRequestData*>(
            RequestContext::get()->getContextData("global"))
            ->key());
  }
}

TEST_F(TaskTest, ContextPreservedAcrossMutexLock) {
  folly::coro::Mutex mutex;

  auto handleRequest =
      [&](folly::coro::Baton& event) -> folly::coro::Task<void> {
    RequestContextScopeGuard requestScope;

    RequestData* contextDataPtr = nullptr;
    {
      auto contextData = std::make_unique<TestRequestData>("some value");
      contextDataPtr = contextData.get();
      RequestContext::get()->setContextData(
          "mutex_test", std::move(contextData));
    }

    auto lock = co_await mutex.co_scoped_lock();

    // Check that the request context was preserved across mutex lock
    // acquisition.
    CHECK_EQ(
        RequestContext::get()->getContextData("mutex_test"), contextDataPtr);

    co_await event;

    // Check that request context was preserved across baton wait.
    CHECK_EQ(
        RequestContext::get()->getContextData("mutex_test"), contextDataPtr);
  };

  folly::ManualExecutor manualExecutor;

  folly::coro::Baton event1;
  folly::coro::Baton event2;
  auto t1 = handleRequest(event1).scheduleOn(&manualExecutor).start();
  auto t2 = handleRequest(event2).scheduleOn(&manualExecutor).start();

  manualExecutor.drain();

  event1.post();

  manualExecutor.drain();

  event2.post();

  manualExecutor.drain();

  EXPECT_TRUE(t1.isReady());
  EXPECT_TRUE(t2.isReady());

  EXPECT_FALSE(t1.hasException());
  EXPECT_FALSE(t2.hasException());
}

TEST_F(TaskTest, RequestContextSideEffectsArePreserved) {
  auto f =
      [&](folly::coro::Baton& baton) -> folly::coro::detail::InlineTask<void> {
    RequestContext::create();

    RequestContext::get()->setContextData(
        testToken1, std::make_unique<TestRequestData>("test"));

    EXPECT_NE(RequestContext::get()->getContextData(testToken1), nullptr);

    // HACK: Need to use co_viaIfAsync() to ensure request context is preserved
    // across suspend-point.
    co_await folly::coro::co_viaIfAsync(
        &folly::InlineExecutor::instance(), baton);

    EXPECT_NE(RequestContext::get()->getContextData(testToken1), nullptr);

    co_return;
  };

  auto g = [&](folly::coro::Baton& baton) -> folly::coro::Task<void> {
    EXPECT_EQ(RequestContext::get()->getContextData(testToken1), nullptr);
    co_await f(baton);
    EXPECT_NE(RequestContext::get()->getContextData(testToken1), nullptr);
    EXPECT_EQ(
        dynamic_cast<TestRequestData*>(
            RequestContext::get()->getContextData(testToken1))
            ->key(),
        "test");
  };

  folly::ManualExecutor executor;
  folly::coro::Baton baton;

  auto t = g(baton).scheduleOn(&executor).start();

  executor.drain();

  baton.post();

  executor.drain();

  EXPECT_TRUE(t.isReady());
  EXPECT_FALSE(t.hasException());
}

TEST_F(TaskTest, FutureTailCall) {
  EXPECT_EQ(
      42,
      folly::coro::blockingWait(
          folly::coro::co_invoke([&]() -> folly::coro::Task<int> {
            co_return co_await folly::makeSemiFuture().deferValue(
                [](auto) { return folly::makeSemiFuture(42); });
          })));
}

TEST_F(TaskTest, FutureRoundtrip) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    co_yield folly::coro::co_result(co_await folly::coro::co_awaitTry(
        []() -> folly::coro::Task<void> { co_return; }().semi()));
  }());

  EXPECT_THROW(
      folly::coro::blockingWait([]() -> folly::coro::Task<void> {
        co_yield folly::coro::co_result(co_await folly::coro::co_awaitTry(
            []() -> folly::coro::Task<void> {
              co_yield folly::coro::co_error(std::runtime_error(""));
            }()
                        .semi()));
      }()),
      std::runtime_error);
}

namespace {

// We just want to make sure this compiles without errors or warnings.
FOLLY_MAYBE_UNUSED folly::coro::Task<void>
checkAwaitingFutureOfUnitDoesntWarnAboutDiscardedResult() {
  co_await folly::makeSemiFuture();

  using namespace std::literals::chrono_literals;
  co_await folly::futures::sleep(1ms);
}

} // namespace

TEST_F(TaskTest, TaskOfLvalueReference) {
  auto returnIntRef = [](int& value) -> folly::coro::Task<int&> {
    co_return value;
  };

  int value = 123;
  auto&& result = folly::coro::blockingWait(returnIntRef(value));
  static_assert(std::is_same_v<decltype(result), int&>);
  CHECK_EQ(&value, &result);
}

TEST_F(TaskTest, TaskOfLvalueReferenceAsTry) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    auto returnIntRef = [](int& value) -> folly::coro::Task<int&> {
      co_return value;
    };

    int value = 123;
    auto&& result = co_await co_awaitTry(returnIntRef(value));
    CHECK(result.hasValue());
    CHECK_EQ(&value, &result.value().get());

    int& valueRef = co_await returnIntRef(value);
    CHECK_EQ(&value, &valueRef);
  }());
}

TEST_F(TaskTest, CancellationPropagation) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    auto token = co_await folly::coro::co_current_cancellation_token;
    CHECK(!token.canBeCancelled());

    folly::CancellationSource cancelSource;

    co_await folly::coro::co_withCancellation(
        cancelSource.getToken(), [&]() -> folly::coro::Task<void> {
          auto token2 = co_await folly::coro::co_current_cancellation_token;
          CHECK(token2.canBeCancelled());
          CHECK(!token2.isCancellationRequested());

          // The cancellation token should implicitly propagate into the
          //
          co_await [&]() -> folly::coro::Task<void> {
            auto token3 = co_await folly::coro::co_current_cancellation_token;
            CHECK(token3 == token2);
            cancelSource.requestCancellation();
            CHECK(token3.isCancellationRequested());
          }();
          CHECK(token2.isCancellationRequested());
        }());
  }());
}

TEST_F(TaskTest, CancellationPropagatesThroughCoAwaitTry) {
  folly::CancellationSource source;
  folly::Try<int> result =
      folly::coro::blockingWait(folly::coro::co_withCancellation(
          source.getToken(),
          folly::coro::co_awaitTry([&]() -> folly::coro::Task<int> {
            auto cancelToken =
                co_await folly::coro::co_current_cancellation_token;
            EXPECT_TRUE(cancelToken == source.getToken());
            co_return 42;
          }())));
  EXPECT_EQ(42, result.value());
}

TEST_F(TaskTest, StartInlineUnsafe) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    auto executor = co_await folly::coro::co_current_executor;
    bool hasStarted = false;
    bool hasFinished = false;
    auto makeTask = [&]() -> folly::coro::Task<void> {
      hasStarted = true;
      co_await folly::coro::co_reschedule_on_current_executor;
      hasFinished = true;
    };
    auto sf = makeTask().scheduleOn(executor).startInlineUnsafe();

    // Check that the task started inline on the current thread.
    // It should not yet have completed, however, since the rest
    // of the coroutine needs this coroutine to suspend so the
    // executor can schedule the rest of it.
    CHECK(hasStarted);
    CHECK(!hasFinished);

    co_await std::move(sf);

    CHECK(hasFinished);
  }());
}

TEST_F(TaskTest, YieldTry) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    auto innerTaskVoid = []() -> folly::coro::Task<void> {
      co_yield folly::coro::co_error(std::runtime_error(""));
    }();
    auto retVoid = co_await co_awaitTry([&]() -> folly::coro::Task<void> {
      co_yield folly::coro::co_result(
          co_await co_awaitTry(std::move(innerTaskVoid)));
    }());
    EXPECT_TRUE(retVoid.hasException());

    innerTaskVoid = []() -> folly::coro::Task<void> { co_return; }();
    retVoid = co_await co_awaitTry([&]() -> folly::coro::Task<void> {
      co_yield folly::coro::co_result(
          co_await co_awaitTry(std::move(innerTaskVoid)));
    }());
    EXPECT_FALSE(retVoid.hasException());

    auto innerTaskInt = []() -> folly::coro::Task<int> {
      co_yield folly::coro::co_error(std::runtime_error(""));
    }();
    auto retInt = co_await co_awaitTry([&]() -> folly::coro::Task<int> {
      co_yield folly::coro::co_result(
          co_await co_awaitTry(std::move(innerTaskInt)));
    }());
    EXPECT_TRUE(retInt.hasException());

    innerTaskInt = []() -> folly::coro::Task<int> { co_return 0; }();
    retInt = co_await co_awaitTry([&]() -> folly::coro::Task<int> {
      co_yield folly::coro::co_result(
          co_await co_awaitTry(std::move(innerTaskInt)));
    }());
    EXPECT_TRUE(retInt.hasValue());

    EXPECT_THROW(
        co_await [&]() -> folly::coro::Task<int> {
          co_yield folly::coro::co_result(folly::Try<int>());
        }(),
        UsingUninitializedTry);
  }());
}

TEST_F(TaskTest, MakeTask) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    auto ret = co_await folly::coro::makeTask(42);
    EXPECT_EQ(ret, 42);

    co_await folly::coro::makeTask();
    co_await folly::coro::makeTask(folly::unit);

    auto err = co_await co_awaitTry(folly::coro::makeErrorTask<int>(
        folly::make_exception_wrapper<std::runtime_error>("")));
    EXPECT_TRUE(err.hasException());

    err = co_await co_awaitTry(folly::coro::makeResultTask(folly::Try<int>(
        folly::make_exception_wrapper<std::runtime_error>(""))));
    EXPECT_TRUE(err.hasException());

    auto try1 = co_await co_awaitTry(
        folly::coro::makeResultTask(folly::Try<folly::Unit>(
            folly::make_exception_wrapper<std::runtime_error>(""))));
    EXPECT_TRUE(try1.hasException());
    try1 = co_await co_awaitTry(
        folly::coro::makeResultTask(folly::Try<folly::Unit>(folly::unit)));
    EXPECT_TRUE(try1.hasValue());

    // test move happens immediately (i.e. no dangling reference)
    struct {
      int i{0};
    } s;
    auto t = folly::coro::makeTask(std::move(s));
    s.i = 1;
    auto s2 = co_await std::move(t);
    EXPECT_EQ(s2.i, 0);
  }());
}

TEST_F(TaskTest, ScheduleOnRestoresExecutor) {
  folly::ScopedEventBaseThread ebt;
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    co_await [&]() -> folly::coro::Task<void> {
      EXPECT_TRUE(ebt.getEventBase()->inRunningEventBaseThread());
      co_return;
    }()
                          .scheduleOn(&ebt);
    EXPECT_FALSE(ebt.getEventBase()->inRunningEventBaseThread());
    try {
      co_await [&]() -> folly::coro::Task<void> {
        EXPECT_TRUE(ebt.getEventBase()->inRunningEventBaseThread());
        throw std::runtime_error("");
        co_return;
      }()
                            .scheduleOn(&ebt);
    } catch (...) {
    }
    EXPECT_FALSE(ebt.getEventBase()->inRunningEventBaseThread());
  }());
}

TEST_F(TaskTest, CoAwaitTryWithScheduleOn) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    auto t = []() -> folly::coro::Task<int> { co_return 42; }();

    folly::Try<int> result = co_await folly::coro::co_awaitTry(
        std::move(t).scheduleOn(folly::getGlobalCPUExecutor()));
    EXPECT_EQ(42, result.value());
  }());
}

TEST_F(TaskTest, CoAwaitTryWithScheduleOnAndCancellation) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    folly::CancellationSource cancelSrc;

    auto makeTask = [&]() -> folly::coro::Task<int> {
      auto ct = co_await folly::coro::co_current_cancellation_token;
      EXPECT_FALSE(ct.isCancellationRequested());
      cancelSrc.requestCancellation();
      EXPECT_TRUE(ct.isCancellationRequested());
      co_return 42;
    };

    {
      folly::Try<int> result = co_await folly::coro::co_withCancellation(
          cancelSrc.getToken(),
          folly::coro::co_awaitTry(
              makeTask().scheduleOn(folly::getGlobalCPUExecutor())));
      EXPECT_EQ(42, result.value());
    }

    cancelSrc = {};

    {
      folly::Try<int> result =
          co_await folly::coro::co_awaitTry(folly::coro::co_withCancellation(
              cancelSrc.getToken(),
              makeTask().scheduleOn(folly::getGlobalCPUExecutor())));
      EXPECT_EQ(42, result.value());
    }
  }());
}

TEST_F(TaskTest, Moved) {
  if (folly::kIsDebug) {
    ASSERT_DEATH_IF_SUPPORTED(
        folly::coro::blockingWait([]() -> folly::coro::Task<void> {
          folly::coro::Task<int> task = folly::coro::makeTask(1);
          co_await std::move(task);
          co_await std::move(task);
        }()),
        "task\\.coro_");
  }
}

TEST_F(TaskTest, SafePoint) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    enum class step_type {
      init,
      before_continue_sp,
      after_continue_sp,
      before_cancel_sp,
      after_cancel_sp,
    };
    step_type step = step_type::init;

    folly::CancellationSource cancelSrc;
    auto makeTask = [&]() -> folly::coro::Task<void> {
      step = step_type::before_continue_sp;
      co_await folly::coro::co_safe_point;
      step = step_type::after_continue_sp;

      cancelSrc.requestCancellation();

      step = step_type::before_cancel_sp;
      co_await folly::coro::co_safe_point;
      step = step_type::after_cancel_sp;
    };

    auto result = co_await folly::coro::co_awaitTry( //
        folly::coro::co_withCancellation(cancelSrc.getToken(), makeTask()));
    EXPECT_THROW(result.value(), folly::OperationCancelled);
    EXPECT_EQ(step_type::before_cancel_sp, step);
  }());
}

TEST_F(TaskTest, CoAwaitNothrow) {
  auto res =
      folly::coro::blockingWait(co_awaitTry([]() -> folly::coro::Task<void> {
        auto t = []() -> folly::coro::Task<int> { co_return 42; }();

        int result = co_await folly::coro::co_nothrow(std::move(t));
        EXPECT_EQ(42, result);

        t = []() -> folly::coro::Task<int> {
          co_yield folly::coro::co_error(std::runtime_error(""));
        }();

        try {
          result = co_await folly::coro::co_nothrow(std::move(t));
        } catch (...) {
          ADD_FAILURE();
        }
        ADD_FAILURE();
      }()));
  EXPECT_TRUE(res.hasException<std::runtime_error>());
}

TEST_F(TaskTest, CoAwaitNothrowWithScheduleOn) {
  auto res =
      folly::coro::blockingWait(co_awaitTry([]() -> folly::coro::Task<void> {
        auto t = []() -> folly::coro::Task<int> { co_return 42; }();

        int result = co_await folly::coro::co_nothrow(
            std::move(t).scheduleOn(folly::getGlobalCPUExecutor()));
        EXPECT_EQ(42, result);

        t = []() -> folly::coro::Task<int> {
          co_yield folly::coro::co_error(std::runtime_error(""));
        }();

        try {
          result = co_await folly::coro::co_nothrow(
              std::move(t).scheduleOn(folly::getGlobalCPUExecutor()));
        } catch (...) {
          ADD_FAILURE();
        }
        ADD_FAILURE();
      }()));
  EXPECT_TRUE(res.hasException<std::runtime_error>());
}

TEST_F(TaskTest, CoAwaitThrowAfterNothrow) {
  auto res =
      folly::coro::blockingWait(co_awaitTry([]() -> folly::coro::Task<void> {
        auto t = []() -> folly::coro::Task<int> { co_return 42; }();

        int result = co_await folly::coro::co_nothrow(std::move(t));
        EXPECT_EQ(42, result);

        t = []() -> folly::coro::Task<int> {
          co_yield folly::coro::co_error(std::runtime_error(""));
        }();

        try {
          result = co_await std::move(t);
          ADD_FAILURE();
        } catch (...) {
          throw std::logic_error("translated");
        }
      }()));
  EXPECT_TRUE(res.hasException<std::logic_error>());
}

TEST_F(TaskTest, CoAwaitNothrowDestructorOrdering) {
  int i = 0;
  folly::coro::blockingWait(co_awaitTry([&]() -> folly::coro::Task<> {
    co_await folly::coro::co_nothrow([&]() -> folly::coro::Task<> {
      SCOPE_EXIT { i *= i; };
      co_await folly::coro::co_nothrow([&]() -> folly::coro::Task<> {
        SCOPE_EXIT { i *= 3; };
        co_await folly::coro::co_nothrow([&]() -> folly::coro::Task<> {
          SCOPE_EXIT { i += 1; };
          co_return;
        }());
      }());
    }());
  }()));
  EXPECT_EQ(i, 9);
}
#endif
