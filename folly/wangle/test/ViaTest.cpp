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

#include <folly/wangle/Future.h>
#include <folly/wangle/InlineExecutor.h>
#include <folly/wangle/ManualExecutor.h>

using namespace folly::wangle;

struct ManualWaiter {
  explicit ManualWaiter(std::shared_ptr<ManualExecutor> ex) : ex(ex) {}

  void makeProgress() {
    ex->wait();
    ex->run();
  }

  std::shared_ptr<ManualExecutor> ex;
};

struct ViaFixture : public testing::Test {
  ViaFixture() :
    future_(makeFuture().deactivate()),
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

  ~ViaFixture() {
    done = true;
    eastExecutor->add([=]() { });
    t.join();
  }

  void addAsync(int a, int b, std::function<void(int&&)>&& cob) {
    eastExecutor->add([=]() {
      cob(a + b);
    });
  }

  Future<void> future_;
  std::shared_ptr<ManualExecutor> westExecutor;
  std::shared_ptr<ManualExecutor> eastExecutor;
  std::shared_ptr<ManualWaiter> waiter;
  InlineExecutor inlineExecutor;
  bool done;
  std::thread t;
};

TEST(Via, exception_on_launch) {
  auto future = makeFuture<int>(std::runtime_error("E"));
  EXPECT_THROW(future.value(), std::runtime_error);
}

TEST(Via, then_value) {
  auto future = makeFuture(std::move(1))
    .then([](Try<int>&& t) {
      return t.value() == 1;
    })
    ;

  EXPECT_TRUE(future.value());
}

TEST(Via, then_future) {
  auto future = makeFuture(1)
    .then([](Try<int>&& t) {
      return makeFuture(t.value() == 1);
    })
    ;
  EXPECT_TRUE(future.value());
}

static Future<std::string> doWorkStatic(Try<std::string>&& t) {
  return makeFuture(t.value() + ";static");
}

TEST(Via, then_function) {
  struct Worker {
    Future<std::string> doWork(Try<std::string>&& t) {
      return makeFuture(t.value() + ";class");
    }
    static Future<std::string> doWorkStatic(Try<std::string>&& t) {
      return makeFuture(t.value() + ";class-static");
    }
  } w;

  auto f = makeFuture(std::string("start"))
    .then(doWorkStatic)
    .then(Worker::doWorkStatic)
    .then(&w, &Worker::doWork)
    ;

  EXPECT_EQ(f.value(), "start;static;class-static;class");
}

TEST_F(ViaFixture, thread_hops) {
  auto westThreadId = std::this_thread::get_id();
  auto f = future_.via(eastExecutor.get()).then([=](Try<void>&& t) {
    EXPECT_NE(std::this_thread::get_id(), westThreadId);
    return makeFuture<int>(1);
  }).via(westExecutor.get()
  ).then([=](Try<int>&& t) {
    EXPECT_EQ(std::this_thread::get_id(), westThreadId);
    return t.value();
  });
  while (!f.isReady()) {
    waiter->makeProgress();
  }
  EXPECT_EQ(f.value(), 1);
}

TEST_F(ViaFixture, chain_vias) {
  auto westThreadId = std::this_thread::get_id();
  auto f = future_.via(eastExecutor.get()).then([=](Try<void>&& t) {
    EXPECT_NE(std::this_thread::get_id(), westThreadId);
    return makeFuture<int>(1);
  }).then([=](Try<int>&& t) {
    int val = t.value();
    return makeFuture(std::move(val)).via(westExecutor.get())
      .then([=](Try<int>&& t) mutable {
        EXPECT_EQ(std::this_thread::get_id(), westThreadId);
        return t.value();
      });
  }).then([=](Try<int>&& t) {
    EXPECT_EQ(std::this_thread::get_id(), westThreadId);
    return t.value();
  });

  while (!f.isReady()) {
    waiter->makeProgress();
  }
  EXPECT_EQ(f.value(), 1);
}

