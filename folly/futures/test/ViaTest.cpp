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

#include <folly/futures/Future.h>
#include <folly/futures/InlineExecutor.h>
#include <folly/futures/ManualExecutor.h>

using namespace folly;

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

TEST_F(ViaFixture, deactivateChain) {
  bool flag = false;
  auto f = makeFuture().deactivate();
  EXPECT_FALSE(f.isActive());
  auto f2 = f.then([&](Try<void>){ flag = true; });
  EXPECT_FALSE(flag);
}

TEST_F(ViaFixture, deactivateActivateChain) {
  bool flag = false;
  // you can do this all day long with temporaries.
  auto f1 = makeFuture().deactivate().activate().deactivate();
  // Chaining on activate/deactivate requires an rvalue, so you have to move
  // one of these two ways (if you're not using a temporary).
  auto f2 = std::move(f1).activate();
  f2.deactivate();
  auto f3 = std::move(f2.activate());
  f3.then([&](Try<void>){ flag = true; });
  EXPECT_TRUE(flag);
}

TEST_F(ViaFixture, thread_hops) {
  auto westThreadId = std::this_thread::get_id();
  auto f = via(eastExecutor.get()).then([=](Try<void>&& t) {
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
  auto f = via(eastExecutor.get()).then([=](Try<void>&& t) {
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

TEST_F(ViaFixture, bareViaAssignment) {
  auto f = via(eastExecutor.get());
}
TEST_F(ViaFixture, viaAssignment) {
  // via()&&
  auto f = makeFuture().via(eastExecutor.get());
  // via()&
  auto f2 = f.via(eastExecutor.get());
}
