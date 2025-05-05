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

#include <folly/Optional.h>
#include <folly/ScopeGuard.h>
#include <folly/executors/ManualExecutor.h>
#include <folly/experimental/coro/Baton.h>
#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/Coroutine.h>
#include <folly/experimental/coro/Invoke.h>
#include <folly/fibers/FiberManager.h>
#include <folly/fibers/FiberManagerMap.h>
#include <folly/portability/GTest.h>

#include <memory>
#include <type_traits>

#if FOLLY_HAS_COROUTINES

static_assert(
    std::is_same<
        decltype(folly::coro::blockingWait(
            std::declval<folly::coro::ready_awaitable<>>())),
        void>::value,
    "");
static_assert(
    std::is_same<
        decltype(folly::coro::blockingWait(
            std::declval<folly::coro::ready_awaitable<int>>())),
        int>::value,
    "");
static_assert(
    std::is_same<
        decltype(folly::coro::blockingWait(
            std::declval<folly::coro::ready_awaitable<int&>>())),
        int&>::value,
    "");
static_assert(
    std::is_same<
        decltype(folly::coro::blockingWait(
            std::declval<folly::coro::ready_awaitable<int&&>>())),
        int>::value,
    "blockingWait() should convert rvalue-reference-returning awaitables "
    "into a returned prvalue to avoid potential lifetime issues since "
    "its possible the rvalue reference could have been to some temporary "
    "object stored inside the Awaiter which would have been destructed "
    "by the time blockingWait returns.");

class BlockingWaitTest : public testing::Test {};

TEST_F(BlockingWaitTest, SynchronousCompletionVoidResult) {
  folly::coro::blockingWait(folly::coro::ready_awaitable<>{});
}

TEST_F(BlockingWaitTest, SynchronousCompletionPRValueResult) {
  EXPECT_EQ(
      123, folly::coro::blockingWait(folly::coro::ready_awaitable<int>{123}));
  EXPECT_EQ(
      "hello",
      folly::coro::blockingWait(
          folly::coro::ready_awaitable<std::string>("hello")));
}

TEST_F(BlockingWaitTest, SynchronousCompletionLValueResult) {
  int value = 123;
  int& result =
      folly::coro::blockingWait(folly::coro::ready_awaitable<int&>{value});
  EXPECT_EQ(&value, &result);
  EXPECT_EQ(123, result);
}

TEST_F(BlockingWaitTest, SynchronousCompletionRValueResult) {
  auto p = std::make_unique<int>(123);
  auto* ptr = p.get();

  // Should return a prvalue which will lifetime-extend when assigned to an
  // auto&& local variable.
  auto&& result = folly::coro::blockingWait(
      folly::coro::ready_awaitable<std::unique_ptr<int>&&>{std::move(p)});

  EXPECT_EQ(ptr, result.get());
  EXPECT_FALSE(p);
}

struct TrickyAwaitable {
  struct Awaiter {
    std::unique_ptr<int> value_;

    bool await_ready() const { return false; }

    bool await_suspend(folly::coro::coroutine_handle<>) {
      value_ = std::make_unique<int>(42);
      return false;
    }

    std::unique_ptr<int>&& await_resume() { return std::move(value_); }
  };

  Awaiter operator co_await() { return {}; }
};

TEST_F(BlockingWaitTest, ReturnRvalueReferenceFromAwaiter) {
  // This awaitable stores the result in the temporary Awaiter object that
  // is placed on the coroutine frame as part of the co_await expression.
  // It then returns an rvalue-reference to the value inside this temporary
  // Awaiter object. This test is making sure that we copy/move the result
  // before destructing the Awaiter object.
  auto result = folly::coro::blockingWait(TrickyAwaitable{});
  CHECK(result);
  CHECK_EQ(42, *result);
}

TEST_F(BlockingWaitTest, AsynchronousCompletionOnAnotherThread) {
  folly::coro::Baton baton;
  std::thread t{[&] { baton.post(); }};
  SCOPE_EXIT { t.join(); };
  folly::coro::blockingWait(baton);
}

template <typename T>
class SimplePromise {
 public:
  class WaitOperation {
   public:
    explicit WaitOperation(
        folly::coro::Baton& baton, folly::Optional<T>& value) noexcept
        : awaiter_(baton), value_(value) {}

    bool await_ready() { return awaiter_.await_ready(); }

    template <typename Promise>
    auto await_suspend(folly::coro::coroutine_handle<Promise> h) {
      return awaiter_.await_suspend(h);
    }

    T&& await_resume() {
      awaiter_.await_resume();
      return std::move(*value_);
    }

   private:
    folly::coro::Baton::WaitOperation awaiter_;
    folly::Optional<T>& value_;
  };

  SimplePromise() = default;

  WaitOperation operator co_await() { return WaitOperation{baton_, value_}; }

  template <typename... Args>
  void emplace(Args&&... args) {
    value_.emplace(static_cast<Args&&>(args)...);
    baton_.post();
  }

 private:
  folly::coro::Baton baton_;
  folly::Optional<T> value_;
};

TEST_F(BlockingWaitTest, WaitOnSimpleAsyncPromise) {
  SimplePromise<std::string> p;
  std::thread t{[&] { p.emplace("hello coroutines!"); }};
  SCOPE_EXIT { t.join(); };
  auto result = folly::coro::blockingWait(p);
  EXPECT_EQ("hello coroutines!", result);
}

struct MoveCounting {
  int count_;
  MoveCounting() noexcept : count_(0) {}
  MoveCounting(MoveCounting&& other) noexcept : count_(other.count_ + 1) {}
  MoveCounting& operator=(MoveCounting&& other) = delete;
};

TEST_F(BlockingWaitTest, WaitOnMoveOnlyAsyncPromise) {
  SimplePromise<MoveCounting> p;
  std::thread t{[&] { p.emplace(); }};
  SCOPE_EXIT { t.join(); };
  auto result = folly::coro::blockingWait(p);

  // Number of move-constructions:
  // 0. Value is in-place constructed in Optional<T>
  // 0. await_resume() returns rvalue reference to Optional<T> value.
  // 1. return_value() moves value into Try<T>
  // 2. Value is moved from Try<T> to blockingWait() return value.
  EXPECT_GE(2, result.count_);
}

TEST_F(BlockingWaitTest, moveCountingAwaitableReady) {
  folly::coro::ready_awaitable<MoveCounting> awaitable{MoveCounting{}};
  auto result = folly::coro::blockingWait(awaitable);

  // Moves:
  // 1. Move value into ready_awaitable
  // 2. Move value to await_resume() return-value
  // 3. Move value to Try<T>
  // 4. Move value to blockingWait() return-value
  EXPECT_GE(4, result.count_);
}

TEST_F(BlockingWaitTest, WaitInFiber) {
  SimplePromise<int> promise;
  folly::EventBase evb;
  auto& fm = folly::fibers::getFiberManager(evb);

  auto future =
      fm.addTaskFuture([&] { return folly::coro::blockingWait(promise); });

  evb.loopOnce();
  EXPECT_FALSE(future.isReady());

  promise.emplace(42);

  evb.loopOnce();
  EXPECT_TRUE(future.isReady());
  EXPECT_EQ(42, std::move(future).get());
}

TEST_F(BlockingWaitTest, WaitTaskInFiber) {
  SimplePromise<int> promise;
  folly::EventBase evb;
  auto& fm = folly::fibers::getFiberManager(evb);

  auto future = fm.addTaskFuture([&] {
    return folly::coro::blockingWait(
        folly::coro::co_invoke([&]() -> folly::coro::Task<int> {
          EXPECT_FALSE(folly::fibers::onFiber());
          auto ret = co_await promise;
          EXPECT_FALSE(folly::fibers::onFiber());
          co_return ret;
        }));
  });

  evb.loopOnce();
  EXPECT_FALSE(future.isReady());

  promise.emplace(42);

  evb.loopOnce();
  EXPECT_TRUE(future.isReady());
  EXPECT_EQ(42, std::move(future).get());
}

struct ExpectedException {};

TEST_F(BlockingWaitTest, WaitTaskInFiberException) {
  folly::EventBase evb;
  auto& fm = folly::fibers::getFiberManager(evb);
  EXPECT_TRUE(
      fm.addTaskFuture([&] {
          try {
            folly::coro::blockingWait(
                folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
                  folly::via(
                      co_await folly::coro::co_current_executor, []() {});
                  throw ExpectedException();
                }));
            return false;
          } catch (const ExpectedException&) {
            return true;
          }
        }).getVia(&evb));
}

TEST_F(BlockingWaitTest, WaitOnSemiFuture) {
  int result = folly::coro::blockingWait(folly::makeSemiFuture(123));
  CHECK_EQ(result, 123);
}

TEST_F(BlockingWaitTest, RequestContext) {
  folly::RequestContext::create();
  std::shared_ptr<folly::RequestContext> ctx1, ctx2;
  ctx1 = folly::RequestContext::saveContext();
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    EXPECT_EQ(ctx1.get(), folly::RequestContext::get());
    folly::RequestContextScopeGuard guard;
    ctx2 = folly::RequestContext::saveContext();
    EXPECT_NE(ctx1, ctx2);
    co_await folly::coro::co_reschedule_on_current_executor;
    EXPECT_EQ(ctx2.get(), folly::RequestContext::get());
    co_return;
  }());
  EXPECT_EQ(ctx1.get(), folly::RequestContext::get());
}

TEST_F(BlockingWaitTest, DrivableExecutor) {
  folly::ManualExecutor executor;
  folly::coro::blockingWait(
      [&]() -> folly::coro::Task<void> {
        folly::Executor* taskExecutor =
            co_await folly::coro::co_current_executor;
        EXPECT_EQ(&executor, taskExecutor);
      }(),
      &executor);
}

TEST_F(BlockingWaitTest, ReleaseExecutorFromAnotherThread) {
  auto fn = []() {
    auto c1 = folly::makePromiseContract<folly::Executor::KeepAlive<>>();
    auto c2 = folly::makePromiseContract<folly::Unit>();
    std::thread t{[&] {
      auto e = std::move(c1.second).get();
      c2.first.setValue(folly::Unit{});
      std::this_thread::sleep_for(std::chrono::microseconds(1));
      e = {};
    }};
    folly::ManualExecutor executor;
    folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
      folly::Executor::KeepAlive<> taskExecutor =
          co_await folly::coro::co_current_executor;
      c1.first.setValue(std::move(taskExecutor));
      co_await std::move(c2.second);
    }());
    t.join();
  };
  std::vector<std::thread> threads;
  for (int i = 0; i < 100; ++i) {
    threads.emplace_back(fn);
  }
  for (auto& t : threads) {
    t.join();
  }
}

#endif
