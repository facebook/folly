/*
 * Copyright 2017-present Facebook, Inc.
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

#include <folly/executors/InlineExecutor.h>
#include <folly/executors/ManualExecutor.h>
#include <folly/experimental/coro/Baton.h>
#include <folly/experimental/coro/Mutex.h>
#include <folly/experimental/coro/Task.h>
#include <folly/experimental/coro/detail/InlineTask.h>
#include <folly/portability/GTest.h>

using namespace folly;

namespace {

const RequestToken testToken1("corotest1");
const RequestToken testToken2("corotest2");

class TestRequestData : public RequestData {
 public:
  explicit TestRequestData(std::string key) noexcept : key_(std::move(key)) {}

  bool hasCallback() override {
    return false;
  }

  const std::string& key() const noexcept {
    return key_;
  }

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

TEST(Task, RequestContextIsPreservedAcrossSuspendResume) {
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

TEST(Task, ContextPreservedAcrossMutexLock) {
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

TEST(Task, RequestContextSideEffectsArePreserved) {
  auto f =
      [&](folly::coro::Baton& baton) -> folly::coro::detail::InlineTask<void> {
    RequestContext::create();

    RequestContext::get()->setContextData(
        testToken1, std::make_unique<TestRequestData>("test"));

    EXPECT_NE(RequestContext::get()->getContextData(testToken1), nullptr);

    // HACK: Need to use co_viaIfAsync() to ensure request context is preserved
    // across suspend-point.
    co_await co_viaIfAsync(&folly::InlineExecutor::instance(), baton);

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

TEST(Task, FutureTailCall) {
  folly::ManualExecutor executor;
  EXPECT_EQ(
      42,
      folly::coro::co_invoke([&]() -> folly::coro::Task<int> {
        co_return co_await folly::makeSemiFuture().deferValue(
            [](auto) { return folly::makeSemiFuture(42); });
      })
          .scheduleOn(&executor)
          .start()
          .via(&executor)
          .getVia(&executor));
}

#endif
