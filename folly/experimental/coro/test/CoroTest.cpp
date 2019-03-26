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

#include <folly/Chrono.h>
#include <folly/executors/ManualExecutor.h>
#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/Task.h>
#include <folly/experimental/coro/Utils.h>
#include <folly/fibers/Semaphore.h>
#include <folly/io/async/ScopedEventBaseThread.h>
#include <folly/portability/GTest.h>

using namespace folly;

coro::Task<int> task42() {
  co_return 42;
}

SemiFuture<int> semifuture_task42() {
  return task42().semi();
}

TEST(Coro, Basic) {
  ManualExecutor executor;
  auto future = task42().scheduleOn(&executor).start();

  EXPECT_FALSE(future.isReady());

  executor.drive();

  EXPECT_TRUE(future.isReady());
  EXPECT_EQ(42, std::move(future).get());
}

TEST(Coro, BasicSemiFuture) {
  ManualExecutor executor;
  auto future = semifuture_task42().via(&executor);

  EXPECT_FALSE(future.isReady());

  executor.drive();
  executor.drive();

  EXPECT_TRUE(future.isReady());
  EXPECT_EQ(42, std::move(future).get());
}

TEST(Coro, BasicFuture) {
  ManualExecutor executor;

  auto future = task42().scheduleOn(&executor).start();

  EXPECT_FALSE(future.isReady());

  EXPECT_EQ(42, std::move(future).via(&executor).getVia(&executor));
}

coro::Task<void> taskVoid() {
  (void)co_await task42();
  co_return;
}

TEST(Coro, Basic2) {
  ManualExecutor executor;
  auto future = taskVoid().scheduleOn(&executor).start();

  EXPECT_FALSE(future.isReady());

  executor.drive();

  EXPECT_TRUE(future.isReady());
}

TEST(Coro, TaskOfMoveOnly) {
  auto f = []() -> coro::Task<std::unique_ptr<int>> {
    co_return std::make_unique<int>(123);
  };

  auto p = coro::blockingWait(f().scheduleOn(&InlineExecutor::instance()));
  EXPECT_TRUE(p);
  EXPECT_EQ(123, *p);
}

coro::Task<void> taskSleep() {
  (void)co_await futures::sleep(std::chrono::seconds{1});
  co_return;
}

TEST(Coro, Sleep) {
  ScopedEventBaseThread evbThread;

  auto startTime = std::chrono::steady_clock::now();
  auto task = taskSleep().scheduleOn(evbThread.getEventBase());

  coro::blockingWait(std::move(task));

  // The total time should be roughly 1 second. Some builds, especially
  // optimized ones, may result in slightly less than 1 second, so we perform
  // rounding here.
  auto totalTime = std::chrono::steady_clock::now() - startTime;
  EXPECT_GE(
      chrono::round<std::chrono::seconds>(totalTime), std::chrono::seconds{1});
}

TEST(Coro, ExecutorKeepAlive) {
  auto future = [] {
    ScopedEventBaseThread evbThread;

    return taskSleep().scheduleOn(evbThread.getEventBase()).start();
  }();
  EXPECT_TRUE(future.isReady());
}

struct CountingExecutor : public ManualExecutor {
  bool keepAliveAcquire() override {
    ++keepAliveCounter;
    return true;
  }

  void keepAliveRelease() override {
    --keepAliveCounter;
  }

  size_t keepAliveCounter{0};
};

coro::Task<void> executorRec(int depth) {
  if (depth == 0) {
    co_return;
  }

  auto executor =
      dynamic_cast<CountingExecutor*>(co_await coro::co_current_executor);
  DCHECK(executor);

  // Note, extra keep-alives are being kept by the Futures.
  EXPECT_EQ(3, executor->keepAliveCounter);

  co_await executorRec(depth - 1);
}

TEST(Coro, ExecutorKeepAliveDummy) {
  CountingExecutor executor;
  executorRec(42).scheduleOn(&executor).start().via(&executor).getVia(
      &executor);
}

coro::Task<int> taskException() {
  throw std::runtime_error("Test exception");
  co_return 42;
}

TEST(Coro, Throw) {
  ManualExecutor executor;
  auto future = taskException().scheduleOn(&executor).start();

  EXPECT_FALSE(future.isReady());

  executor.drive();

  EXPECT_TRUE(future.isReady());
  EXPECT_THROW(std::move(future).get(), std::runtime_error);
}

TEST(Coro, FutureThrow) {
  ManualExecutor executor;
  auto future = taskException().scheduleOn(&executor).start();

  EXPECT_FALSE(future.isReady());

  executor.drive();

  EXPECT_TRUE(future.isReady());
  EXPECT_THROW(std::move(future).get(), std::runtime_error);
}

coro::Task<int> taskRecursion(int depth) {
  if (depth > 0) {
    EXPECT_EQ(depth - 1, co_await taskRecursion(depth - 1));
  } else {
    (void)co_await futures::sleep(std::chrono::seconds{1});
  }

  co_return depth;
}

TEST(Coro, LargeStack) {
  ScopedEventBaseThread evbThread;
  auto task = taskRecursion(5000).scheduleOn(evbThread.getEventBase());

  EXPECT_EQ(5000, coro::blockingWait(std::move(task)));
}

coro::Task<void> taskThreadNested(std::thread::id threadId) {
  EXPECT_EQ(threadId, std::this_thread::get_id());
  (void)co_await futures::sleep(std::chrono::seconds{1});
  EXPECT_EQ(threadId, std::this_thread::get_id());
  co_return;
}

coro::Task<int> taskThread() {
  auto threadId = std::this_thread::get_id();

  // BUG: Under @mode/clang-opt builds this object is placed on the coroutine
  // frame and the code for the constructor assumes that it is allocated on
  // a 16-byte boundary. However, when placed in the coroutine frame it can
  // end up at a location that is not 16-byte aligned. This causes a SIGSEGV
  // when performing a store to members that uses SSE instructions.
  folly::ScopedEventBaseThread evbThread;

  co_await taskThreadNested(evbThread.getThreadId())
      .scheduleOn(evbThread.getEventBase());

  EXPECT_EQ(threadId, std::this_thread::get_id());

  co_return 42;
}

TEST(Coro, NestedThreads) {
  ScopedEventBaseThread evbThread;
  auto task = taskThread().scheduleOn(evbThread.getEventBase());
  EXPECT_EQ(42, coro::blockingWait(std::move(task)));
}

coro::Task<int> taskGetCurrentExecutor(Executor* executor) {
  auto current = co_await coro::co_current_executor;
  EXPECT_EQ(executor, current);
  co_return co_await task42().scheduleOn(current);
}

TEST(Coro, CurrentExecutor) {
  ScopedEventBaseThread evbThread;
  auto task = taskGetCurrentExecutor(evbThread.getEventBase())
                  .scheduleOn(evbThread.getEventBase());
  EXPECT_EQ(42, coro::blockingWait(std::move(task)));
}

coro::Task<void> taskTimedWait() {
  auto ex = co_await coro::co_current_executor;
  auto fastFuture =
      futures::sleep(std::chrono::milliseconds{50}).via(ex).thenValue([](Unit) {
        return 42;
      });
  auto fastResult = co_await coro::timed_wait(
      std::move(fastFuture), std::chrono::milliseconds{100});
  EXPECT_TRUE(fastResult);
  EXPECT_EQ(42, *fastResult);

  struct ExpectedException : public std::runtime_error {
    ExpectedException() : std::runtime_error("ExpectedException") {}
  };

  auto throwingFuture =
      futures::sleep(std::chrono::milliseconds{50}).via(ex).thenValue([](Unit) {
        throw ExpectedException();
      });
  EXPECT_THROW(
      (void)co_await coro::timed_wait(
          std::move(throwingFuture), std::chrono::milliseconds{100}),
      ExpectedException);

  auto promiseFuturePair = folly::makePromiseContract<folly::Unit>(ex);
  auto lifetimeFuture = std::move(promiseFuturePair.second);
  auto slowFuture =
      futures::sleep(std::chrono::milliseconds{200})
          .via(ex)
          .thenValue([lifetimePromise =
                          std::move(promiseFuturePair.first)](Unit) mutable {
            lifetimePromise.setValue();
            return 42;
          });
  auto slowResult = co_await coro::timed_wait(
      std::move(slowFuture), std::chrono::milliseconds{100});
  EXPECT_FALSE(slowResult);

  // Ensure that task completes for safe executor lifetimes
  (void)co_await std::move(lifetimeFuture);

  co_return;
}

TEST(Coro, TimedWait) {
  coro::blockingWait(taskTimedWait());
}

template <int value>
struct AwaitableInt {
  bool await_ready() const {
    return true;
  }

  bool await_suspend(std::experimental::coroutine_handle<>) {
    LOG(FATAL) << "Should never be called.";
  }

  int await_resume() {
    return value;
  }
};

struct AwaitableWithOperator {};

AwaitableInt<42> operator co_await(const AwaitableWithOperator&) {
  return {};
}

coro::Task<int> taskAwaitableWithOperator() {
  co_return co_await AwaitableWithOperator();
}

TEST(Coro, AwaitableWithOperator) {
  EXPECT_EQ(42, coro::blockingWait(taskAwaitableWithOperator()));
}

struct AwaitableWithMemberOperator {
  AwaitableInt<42> operator co_await() {
    return {};
  }
};

AwaitableInt<24> operator co_await(const AwaitableWithMemberOperator&) {
  return {};
}

coro::Task<int> taskAwaitableWithMemberOperator() {
  co_return co_await AwaitableWithMemberOperator();
}

TEST(Coro, AwaitableWithMemberOperator) {
  EXPECT_EQ(42, coro::blockingWait(taskAwaitableWithMemberOperator()));
}

coro::Task<int> taskBaton(fibers::Baton& baton) {
  co_await baton;
  co_return 42;
}

TEST(Coro, Baton) {
  ManualExecutor executor;
  fibers::Baton baton;
  auto future = taskBaton(baton).scheduleOn(&executor).start();

  EXPECT_FALSE(future.isReady());

  executor.run();

  EXPECT_FALSE(future.isReady());

  baton.post();
  executor.run();

  EXPECT_TRUE(future.isReady());
  EXPECT_EQ(42, std::move(future).get());
}

template <class Type>
coro::Task<Type> taskFuture(Type value) {
  co_return co_await folly::makeFuture<Type>(std::move(value));
}

TEST(Coro, FulfilledFuture) {
  EXPECT_EQ(42, coro::blockingWait(taskFuture(42)));
}

TEST(Coro, MoveOnlyReturn) {
  EXPECT_EQ(42, *coro::blockingWait(taskFuture(std::make_unique<int>(42))));
}

TEST(Coro, co_invoke) {
  ManualExecutor executor;
  Promise<folly::Unit> p;
  auto coroFuture =
      coro::co_invoke([f = p.getSemiFuture()]() mutable -> coro::Task<void> {
        (void)co_await std::move(f);
        co_return;
      })
          .scheduleOn(&executor)
          .start();
  executor.run();
  EXPECT_FALSE(coroFuture.isReady());

  p.setValue(folly::unit);

  executor.run();
  EXPECT_TRUE(coroFuture.isReady());
}

TEST(Coro, Semaphore) {
  static constexpr size_t kTasks = 10;
  static constexpr size_t kIterations = 10000;
  static constexpr size_t kNumTokens = 10;
  static constexpr size_t kNumThreads = 16;

  fibers::Semaphore sem(kNumTokens);

  struct Worker {
    explicit Worker(fibers::Semaphore& s) : sem(s), t([&] { run(); }) {}

    void run() {
      folly::EventBase evb;

      {
        std::shared_ptr<folly::EventBase> completionCounter(
            &evb, [](folly::EventBase* evb_) { evb_->terminateLoopSoon(); });

        for (size_t i = 0; i < kTasks; ++i) {
          coro::co_invoke([&, completionCounter]() -> coro::Task<void> {
            for (size_t j = 0; j < kIterations; ++j) {
              co_await sem.co_wait();
              ++counter;
              sem.signal();
              --counter;

              EXPECT_LT(counter, kNumTokens);
              EXPECT_GE(counter, 0);
            }
          })
              .scheduleOn(&evb)
              .start();
        }
      }
      evb.loopForever();
    }

    fibers::Semaphore& sem;
    int counter{0};
    std::thread t;
  };

  std::vector<Worker> workers;
  workers.reserve(kNumThreads);
  for (size_t i = 0; i < kNumThreads; ++i) {
    workers.emplace_back(sem);
  }

  for (auto& worker : workers) {
    worker.t.join();
  }

  for (auto& worker : workers) {
    EXPECT_EQ(0, worker.counter);
  }
}

#endif
