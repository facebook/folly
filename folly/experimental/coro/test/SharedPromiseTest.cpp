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

#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/DetachOnCancel.h>
#include <folly/experimental/coro/SharedPromise.h>

#include <folly/portability/GTest.h>

#if FOLLY_HAS_COROUTINES

using namespace folly::coro;

class BlockingWaitWaitInterface {
 public:
  std::string waitAndGetValue(folly::coro::Future<std::string> future) {
    return folly::coro::blocking_wait(std::move(future));
  }
};

class CPUThreadPoolWaitInterface {
 public:
  std::string waitAndGetValue(folly::coro::Future<std::string> future) {
    return coGet(std::move(future))
        .scheduleOn(cpuThreadPoolExecutor_.get())
        .start()
        .get();
  }

 private:
  folly::coro::Task<std::string> coGet(
      folly::coro::Future<std::string> future) {
    co_return co_await std::move(future);
  }

  std::unique_ptr<folly::CPUThreadPoolExecutor> cpuThreadPoolExecutor_{
      std::make_unique<folly::CPUThreadPoolExecutor>(1)};
};

template <typename WaitInterface>
class ValueInterface : public WaitInterface {
 public:
  void set(std::string value, SharedPromise<std::string>& promise) {
    promise.setValue(std::move(value));
  }

  std::string get(folly::coro::Future<std::string> future) {
    return this->waitAndGetValue(std::move(future));
  }
};

template <typename WaitInterface>
class ExceptionInterface : public WaitInterface {
  class StringException : public std::exception {
   public:
    explicit StringException(std::string string) : string_{string} {}
    std::string get() { return string_; }

   private:
    std::string string_;
  };

 public:
  void set(std::string value, SharedPromise<std::string>& promise) {
    promise.setException(StringException{value});
  }

  std::string get(folly::coro::Future<std::string> future) {
    try {
      this->waitAndGetValue(std::move(future));
    } catch (StringException& exception) {
      return exception.get();
    }

    CHECK(false) << "Expected value in exception, "
                 << " but didn't get any exception";
  }
};

template <typename TestInterface>
class SharedPromiseTest : public ::testing::Test, public TestInterface {};

using TestTypes = ::testing::Types<
    ValueInterface<BlockingWaitWaitInterface>,
    ValueInterface<CPUThreadPoolWaitInterface>,
    ExceptionInterface<BlockingWaitWaitInterface>,
    ExceptionInterface<CPUThreadPoolWaitInterface>>;

TYPED_TEST_SUITE(SharedPromiseTest, TestTypes);

TYPED_TEST(SharedPromiseTest, Basic) {
  auto promise = SharedPromise<std::string>{};
  auto future = promise.getFuture();

  this->set("ynwa", promise);
  EXPECT_EQ("ynwa", this->get(std::move(future)));
}

TYPED_TEST(SharedPromiseTest, MultipleFutures) {
  auto promise = SharedPromise<std::string>{};
  auto futures = std::vector<folly::coro::Future<std::string>>{};
  for (auto i = 0; i < 10; ++i) {
    futures.push_back(promise.getFuture());
  }

  this->set("liverpool_best_club", promise);
  for (auto& future : futures) {
    EXPECT_EQ("liverpool_best_club", this->get(std::move(future)));
  }
}

TYPED_TEST(SharedPromiseTest, BrokenPromise) {
  auto testFutures = []() {
    auto promise = SharedPromise<std::string>{};

    auto futures = std::vector<folly::coro::Future<std::string>>{};
    for (auto i = 0; i < 10; ++i) {
      futures.push_back(promise.getFuture());
    }

    return futures;
  }();

  for (auto& future : testFutures) {
    EXPECT_THROW(this->get(std::move(future)), folly::BrokenPromise);
  }
}

TYPED_TEST(SharedPromiseTest, PromiseAlreadySatisfied) {
  {
    auto promise = SharedPromise<std::string>{};
    this->set("liverpool_epl_champions", promise);

    EXPECT_THROW(this->set("ynwa", promise), folly::PromiseAlreadySatisfied);
  }

  {
    auto promise = SharedPromise<std::string>{};
    auto futures = std::vector<folly::coro::Future<std::string>>{};
    for (auto i = 0; i < 10; ++i) {
      futures.push_back(promise.getFuture());
    }

    this->set("ynwa_1", promise);
    EXPECT_THROW(this->set("ynwa_2", promise), folly::PromiseAlreadySatisfied);

    for (auto& future : futures) {
      EXPECT_EQ("ynwa_1", this->get(std::move(future)));
    }
  }
}

TYPED_TEST(SharedPromiseTest, FutureAfterFulfilled) {
  auto promise = SharedPromise<std::string>{};
  auto futures = std::vector<folly::coro::Future<std::string>>{};
  for (auto i = 0; i < 10; ++i) {
    futures.push_back(promise.getFuture());
  }

  this->set("ynwa", promise);
  for (auto& future : futures) {
    EXPECT_EQ(this->get(std::move(future)), "ynwa");
  }

  EXPECT_EQ(this->get(promise.getFuture()), "ynwa");
}

TYPED_TEST(SharedPromiseTest, PostMoveConstruction) {
  auto promise = SharedPromise<std::string>{};
  auto future = promise.getFuture();

  this->set("ynwa_1", promise);
  auto anotherPromise = std::move(promise);

  EXPECT_EQ(this->get(std::move(future)), "ynwa_1");
  EXPECT_EQ(this->get(anotherPromise.getFuture()), "ynwa_1");

  // @lint-ignore CLANGTIDY
  auto anotherFuture = promise.getFuture();
  this->set("ynwa_2", promise);
  EXPECT_EQ(this->get(std::move(anotherFuture)), "ynwa_2");
  EXPECT_EQ(this->get(promise.getFuture()), "ynwa_2");
}

TYPED_TEST(SharedPromiseTest, PostMoveAssignment) {
  auto promise = SharedPromise<std::string>{};
  auto future = promise.getFuture();

  this->set("ynwa_1", promise);
  auto anotherPromise = SharedPromise<std::string>{};
  anotherPromise = std::move(promise);

  EXPECT_EQ(this->get(std::move(future)), "ynwa_1");
  EXPECT_EQ(this->get(anotherPromise.getFuture()), "ynwa_1");

  // @lint-ignore CLANGTIDY
  auto anotherFuture = promise.getFuture();
  this->set("ynwa_2", promise);
  EXPECT_EQ(this->get(std::move(anotherFuture)), "ynwa_2");
  EXPECT_EQ(this->get(promise.getFuture()), "ynwa_2");
}

namespace {
class FallibleExecutor : public folly::Executor {
 public:
  explicit FallibleExecutor(std::unique_ptr<folly::Executor> executor)
      : executor_{std::move(executor)} {}

  void add(folly::Function<void()> function) override {
    if (!failed_.load()) {
      executor_->add(std::move(function));
      return;
    }

    CHECK(false) << "Cannot add to FallibleExecutor after it has entered the "
                 << "fail state.";
  }

  void fail() { failed_.store(true); }

 private:
  std::unique_ptr<folly::Executor> executor_;
  std::atomic<bool> failed_{false};
};
} // namespace

TYPED_TEST(SharedPromiseTest, CleanlyCancellableWait) {
  // This test makes sure that there are no detached tasks lying around on the
  // executor _after_ the cancellation exception has propagated to the user.  In
  // particular, this bug was found in the falcon codebase and fixed in
  // D39738899.
  //
  // folly::coro::Promise and folly::coro::Future should natively support
  // cancellable waits.  We test for this assertion here.
  //
  // If the promise and future types were to be switched to folly::Promise and
  // folly::Future, this test would never return because cancellation would
  // never get triggered.  Adding detachOnCancel() leaves around a detached
  // coroutine on the executor, which we put in a "failed" state, similar to
  // folly::Future's internal WaitExecutor.  So using detachOnCancel() will
  // cause the test to fail as well
  auto promise = SharedPromise<std::string>{};
  auto future = promise.getFuture();
  auto baton = folly::Baton<>{};

  auto task = folly::coro::co_invoke([&]() -> folly::coro::Task<std::string> {
    co_return co_await std::move(future);
  });

  auto cpu = std::make_unique<folly::CPUThreadPoolExecutor>(1);
  auto fallibleExecutor = std::make_unique<FallibleExecutor>(std::move(cpu));
  auto cancellationSource = folly::CancellationSource{};
  auto cancellationToken = cancellationSource.getToken();

  auto started =
      folly::coro::co_withCancellation(cancellationToken, std::move(task))
          .scheduleOn(fallibleExecutor.get())
          .start();

  cancellationSource.requestCancellation();
  EXPECT_THROW(std::move(started).get(), folly::OperationCancelled);

  // make the executor go in a failed state and set the promise, so if the
  // future was detached, we would get the detached coroutine to run on the
  // executor
  fallibleExecutor->fail();
  promise.setValue("ynwa");
}

TYPED_TEST(SharedPromiseTest, NoHeapAllocation) {
  char allocBuffer[1024];
  auto promise = new (allocBuffer) SharedPromise<std::string>{};
  promise->setValue("foo");
  // If SharedPromise has heap allocation, it would trigger ASAN failures
}

TEST(SharedPromiseTest, BasicVoid) {
  {
    auto promise = SharedPromise<void>{};
    auto future = promise.getFuture();

    promise.setValue();
    blocking_wait(std::move(future));
  }

  {
    auto promise = SharedPromise<void>{};
    promise.setValue();
    blocking_wait(promise.getFuture());
  }
}

#endif
