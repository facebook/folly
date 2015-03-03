/*
 * Copyright 2015 Facebook, Inc.
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
#include <folly/futures/InlineExecutor.h>
#include <folly/futures/ManualExecutor.h>
#include <folly/futures/QueuedImmediateExecutor.h>
#include <folly/futures/Future.h>
#include <folly/Baton.h>

using namespace folly;
using namespace std::chrono;
using namespace testing;

TEST(ManualExecutor, runIsStable) {
  ManualExecutor x;
  size_t count = 0;
  auto f1 = [&]() { count++; };
  auto f2 = [&]() { x.add(f1); x.add(f1); };
  x.add(f2);
  x.run();
}

TEST(ManualExecutor, scheduleDur) {
  ManualExecutor x;
  size_t count = 0;
  milliseconds dur {10};
  x.schedule([&]{ count++; }, dur);
  EXPECT_EQ(count, 0);
  x.run();
  EXPECT_EQ(count, 0);
  x.advance(dur/2);
  EXPECT_EQ(count, 0);
  x.advance(dur/2);
  EXPECT_EQ(count, 1);
}

TEST(ManualExecutor, clockStartsAt0) {
  ManualExecutor x;
  EXPECT_EQ(x.now(), x.now().min());
}

TEST(ManualExecutor, scheduleAbs) {
  ManualExecutor x;
  size_t count = 0;
  x.scheduleAt([&]{ count++; }, x.now() + milliseconds(10));
  EXPECT_EQ(count, 0);
  x.advance(milliseconds(10));
  EXPECT_EQ(count, 1);
}

TEST(ManualExecutor, advanceTo) {
  ManualExecutor x;
  size_t count = 0;
  x.scheduleAt([&]{ count++; }, steady_clock::now());
  EXPECT_EQ(count, 0);
  x.advanceTo(steady_clock::now());
  EXPECT_EQ(count, 1);
}

TEST(ManualExecutor, advanceBack) {
  ManualExecutor x;
  size_t count = 0;
  x.advance(microseconds(5));
  x.schedule([&]{ count++; }, microseconds(6));
  EXPECT_EQ(count, 0);
  x.advanceTo(x.now() - microseconds(1));
  EXPECT_EQ(count, 0);
}

TEST(ManualExecutor, advanceNeg) {
  ManualExecutor x;
  size_t count = 0;
  x.advance(microseconds(5));
  x.schedule([&]{ count++; }, microseconds(6));
  EXPECT_EQ(count, 0);
  x.advance(microseconds(-1));
  EXPECT_EQ(count, 0);
}

TEST(ManualExecutor, waitForDoesNotDeadlock) {
  ManualExecutor east, west;
  folly::Baton<> baton;
  auto f = makeFuture()
    .via(&east)
    .then([](Try<void>){ return makeFuture(); })
    .via(&west);
  std::thread t([&]{
    baton.post();
    west.waitFor(f);
  });
  baton.wait();
  east.run();
  t.join();
}

TEST(Executor, InlineExecutor) {
  InlineExecutor x;
  size_t counter = 0;
  x.add([&]{
    x.add([&]{
      EXPECT_EQ(counter, 0);
      counter++;
    });
    EXPECT_EQ(counter, 1);
    counter++;
  });
  EXPECT_EQ(counter, 2);
}

TEST(Executor, QueuedImmediateExecutor) {
  QueuedImmediateExecutor x;
  size_t counter = 0;
  x.add([&]{
    x.add([&]{
      EXPECT_EQ(1, counter);
      counter++;
    });
    EXPECT_EQ(0, counter);
    counter++;
  });
  EXPECT_EQ(2, counter);
}

TEST(Executor, Runnable) {
  InlineExecutor x;
  size_t counter = 0;
  struct Runnable {
    std::function<void()> fn;
    void operator()() { fn(); }
  };
  Runnable f;
  f.fn = [&]{ counter++; };
  x.add(f);
  EXPECT_EQ(counter, 1);
}

TEST(Executor, RunnablePtr) {
  InlineExecutor x;
  struct Runnable {
    std::function<void()> fn;
    void operator()() { fn(); }
  };
  size_t counter = 0;
  auto fnp = std::make_shared<Runnable>();
  fnp->fn = [&]{ counter++; };
  x.addPtr(fnp);
  EXPECT_EQ(counter, 1);
}

TEST(Executor, ThrowableThen) {
  InlineExecutor x;
  auto f = Future<void>().via(&x).then([](){
    throw std::runtime_error("Faildog");
  });
  EXPECT_THROW(f.value(), std::exception);
}

class CrappyExecutor : public Executor {
 public:
  void add(Func f) override {
    throw std::runtime_error("bad");
  }
};

TEST(Executor, CrappyExecutor) {
  CrappyExecutor x;
  try {
    auto f = Future<void>().via(&x).activate().then([](){
      return;
    });
    f.value();
    EXPECT_TRUE(false);
  } catch(...) {
    // via() should throw
    return;
  }
}
