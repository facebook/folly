/*
 * Copyright 2014 Facebook, Inc.
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

#include <gtest/gtest.h>
#include <thread>

#include <folly/wangle/ManualExecutor.h>
#include <folly/wangle/InlineExecutor.h>
#include <folly/wangle/Later.h>

using namespace folly::wangle;

struct ManualWaiter {
  explicit ManualWaiter(std::shared_ptr<ManualExecutor> ex) : ex(ex) {}

  void makeProgress() {
    ex->wait();
    ex->run();
  }

  std::shared_ptr<ManualExecutor> ex;
};

struct LaterFixture : public testing::Test {
  LaterFixture() :
    westExecutor(new ManualExecutor),
    eastExecutor(new ManualExecutor),
    waiter(new ManualWaiter(westExecutor)),
    done(false)
  {
    t = std::thread([=] {
      ManualWaiter eastWaiter(eastExecutor);
      while (!done)
        eastWaiter.makeProgress();
      });
  }

  ~LaterFixture() {
    done = true;
    eastExecutor->add([=]() { });
    t.join();
  }

  void addAsync(int a, int b, std::function<void(int&&)>&& cob) {
    eastExecutor->add([=]() {
      cob(a + b);
    });
  }

  Later<void> later;
  std::shared_ptr<ManualExecutor> westExecutor;
  std::shared_ptr<ManualExecutor> eastExecutor;
  std::shared_ptr<ManualWaiter> waiter;
  InlineExecutor inlineExecutor;
  bool done;
  std::thread t;
};

TEST(Later, construct_and_launch) {
  bool fulfilled = false;
  auto later = Later<void>().then([&](Try<void>&& t) {
    fulfilled = true;
    return makeFuture<int>(1);
  });

  // has not started yet.
  EXPECT_FALSE(fulfilled);

  EXPECT_EQ(later.launch().value(), 1);
  EXPECT_TRUE(fulfilled);
}

TEST(Later, exception_on_launch) {
  auto later = Later<void>(std::runtime_error("E"));
  EXPECT_THROW(later.launch().value(), std::runtime_error);
}

TEST(Later, then_value) {
  auto future = Later<int>(std::move(1))
    .then([](Try<int>&& t) {
      return t.value() == 1;
    })
    .launch();

  EXPECT_TRUE(future.value());
}

TEST(Later, then_future) {
  auto future = Later<int>(1)
    .then([](Try<int>&& t) {
      return makeFuture(t.value() == 1);
    })
    .launch();
  EXPECT_TRUE(future.value());
}

TEST_F(LaterFixture, thread_hops) {
  auto westThreadId = std::this_thread::get_id();
  auto future = later.via(eastExecutor.get()).then([=](Try<void>&& t) {
    EXPECT_NE(std::this_thread::get_id(), westThreadId);
    return makeFuture<int>(1);
  }).via(westExecutor.get()
  ).then([=](Try<int>&& t) {
    EXPECT_EQ(std::this_thread::get_id(), westThreadId);
    return t.value();
  }).launch();
  while (!future.isReady()) {
    waiter->makeProgress();
  }
  EXPECT_EQ(future.value(), 1);
}

TEST_F(LaterFixture, wrapping_preexisting_async_modules) {
  auto westThreadId = std::this_thread::get_id();
  std::function<void(std::function<void(int&&)>&&)> wrapper =
    [=](std::function<void(int&&)>&& fn) {
      addAsync(2, 2, std::move(fn));
    };
  auto future = Later<int>(std::move(wrapper))
  .via(westExecutor.get())
  .then([=](Try<int>&& t) {
    EXPECT_EQ(std::this_thread::get_id(), westThreadId);
    return t.value();
  })
  .launch();
  while (!future.isReady()) {
    waiter->makeProgress();
  }
  EXPECT_EQ(future.value(), 4);
}

TEST_F(LaterFixture, chain_laters) {
  auto westThreadId = std::this_thread::get_id();
  auto future = later.via(eastExecutor.get()).then([=](Try<void>&& t) {
    EXPECT_NE(std::this_thread::get_id(), westThreadId);
    return makeFuture<int>(1);
  }).then([=](Try<int>&& t) {
    int val = t.value();
    return Later<int>(std::move(val)).via(westExecutor.get())
      .then([=](Try<int>&& t) mutable {
        EXPECT_EQ(std::this_thread::get_id(), westThreadId);
        return t.value();
      });
  }).then([=](Try<int>&& t) {
    EXPECT_EQ(std::this_thread::get_id(), westThreadId);
    return t.value();
  }).launch();

  while (!future.isReady()) {
    waiter->makeProgress();
  }
  EXPECT_EQ(future.value(), 1);
}
