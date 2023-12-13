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

#include <thread>

#include <folly/MPMCQueue.h>
#include <folly/executors/DrivableExecutor.h>
#include <folly/executors/InlineExecutor.h>
#include <folly/executors/ManualExecutor.h>
#include <folly/futures/Future.h>
#include <folly/portability/GTest.h>
#include <folly/synchronization/Baton.h>

using namespace folly;

struct ManualWaiter : public DrivableExecutor {
  explicit ManualWaiter(std::shared_ptr<ManualExecutor> ex_) : ex(ex_) {}

  void add(Func f) override { ex->add(std::move(f)); }

  void drive() override {
    ex->wait();
    ex->run();
  }

  std::shared_ptr<ManualExecutor> ex;
};

struct ViaFixture : public testing::Test {
  ViaFixture()
      : westExecutor(new ManualExecutor),
        eastExecutor(new ManualExecutor),
        waiter(new ManualWaiter(westExecutor)),
        done(false) {
    th = std::thread([=] {
      ManualWaiter eastWaiter(eastExecutor);
      while (!done) {
        eastWaiter.drive();
      }
    });
  }

  ~ViaFixture() override {
    done = true;
    eastExecutor->add([=]() {});
    th.join();
  }

  void addAsync(int a, int b, std::function<void(int&&)>&& cob) {
    eastExecutor->add([=]() { cob(a + b); });
  }

  std::shared_ptr<ManualExecutor> westExecutor;
  std::shared_ptr<ManualExecutor> eastExecutor;
  std::shared_ptr<ManualWaiter> waiter;
  InlineExecutor inlineExecutor;
  std::atomic<bool> done;
  std::thread th;
};

TEST(Via, exceptionOnLaunch) {
  auto future = makeFuture<int>(std::runtime_error("E"));
  EXPECT_THROW(future.value(), std::runtime_error);
}

TEST(Via, thenValue) {
  auto future = makeFuture(std::move(1)).thenTry([](Try<int>&& t) {
    return t.value() == 1;
  });

  EXPECT_TRUE(future.value());
}

TEST(Via, thenFuture) {
  auto future = makeFuture(1).thenTry(
      [](Try<int>&& t) { return makeFuture(t.value() == 1); });
  EXPECT_TRUE(future.value());
}

static Future<std::string> doWorkStatic(Try<std::string>&& t) {
  return makeFuture(t.value() + ";static");
}

TEST(Via, thenFunction) {
  struct Worker {
    Future<std::string> doWork(Try<std::string>&& t) {
      return makeFuture(t.value() + ";class");
    }
    static Future<std::string> doWorkStatic(Try<std::string>&& t) {
      return makeFuture(t.value() + ";class-static");
    }
  } w;

  auto f = makeFuture(std::string("start"))
               .thenTry(doWorkStatic)
               .thenTry(Worker::doWorkStatic)
               .then(&Worker::doWork, &w);

  EXPECT_EQ(f.value(), "start;static;class-static;class");
}

TEST_F(ViaFixture, threadHops) {
  auto westThreadId = std::this_thread::get_id();
  auto f = via(eastExecutor.get())
               .thenTry([=](Try<Unit>&& /* t */) {
                 EXPECT_NE(std::this_thread::get_id(), westThreadId);
                 return makeFuture<int>(1);
               })
               .via(westExecutor.get())
               .thenTry([=](Try<int>&& t) {
                 EXPECT_EQ(std::this_thread::get_id(), westThreadId);
                 return t.value();
               });
  EXPECT_EQ(std::move(f).getVia(waiter.get()), 1);
}

TEST_F(ViaFixture, chainVias) {
  auto westThreadId = std::this_thread::get_id();
  auto f = via(eastExecutor.get())
               .thenValue([=](auto&&) {
                 EXPECT_NE(std::this_thread::get_id(), westThreadId);
                 return 1;
               })
               .thenValue([=](int val) {
                 return makeFuture(val)
                     .via(westExecutor.get())
                     .thenValue([=](int v) mutable {
                       EXPECT_EQ(std::this_thread::get_id(), westThreadId);
                       return v + 1;
                     });
               })
               .thenValue([=](int val) {
                 // even though ultimately the future that triggers this one
                 // executed in the west thread, this thenValue() inherited the
                 // executor from its predecessor, ie the eastExecutor.
                 EXPECT_NE(std::this_thread::get_id(), westThreadId);
                 return val + 1;
               })
               .via(westExecutor.get())
               .thenValue([=](int val) {
                 // go back to west, so we can wait on it
                 EXPECT_EQ(std::this_thread::get_id(), westThreadId);
                 return val + 1;
               });

  EXPECT_EQ(std::move(f).getVia(waiter.get()), 4);
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

struct PriorityExecutor : public Executor {
  void add(Func /* f */) override { count1++; }

  void addWithPriority(Func f, int8_t priority) override {
    int mid = getNumPriorities() / 2;
    int p = priority < 0 ? std::max(0, mid + priority)
                         : std::min(getNumPriorities() - 1, mid + priority);
    EXPECT_LT(p, 3);
    EXPECT_GE(p, 0);
    if (p == 0) {
      count0++;
    } else if (p == 1) {
      count1++;
    } else if (p == 2) {
      count2++;
    }
    f();
  }

  uint8_t getNumPriorities() const override { return 3; }

  int count0{0};
  int count1{0};
  int count2{0};
};

TEST(Via, priority) {
  PriorityExecutor exe;
  via(&exe, -1).thenValue([](auto&&) {});
  via(&exe, 0).thenValue([](auto&&) {});
  via(&exe, 1).thenValue([](auto&&) {});
  via(&exe, 42).thenValue([](auto&&) {}); // overflow should go to max priority
  via(&exe, -42).thenValue(
      [](auto&&) {}); // underflow should go to min priority
  via(&exe).thenValue([](auto&&) {}); // default to mid priority
  via(&exe, Executor::LO_PRI).thenValue([](auto&&) {});
  via(&exe, Executor::HI_PRI).thenValue([](auto&&) {});
  EXPECT_EQ(3, exe.count0);
  EXPECT_EQ(2, exe.count1);
  EXPECT_EQ(3, exe.count2);
}

TEST(Via, then2) {
  ManualExecutor x1, x2;
  bool a = false, b = false, c = false;
  via(&x1)
      .thenValue([&](auto&&) { a = true; })
      .thenValue([&](auto&&) { b = true; })
      .thenValueInline(folly::makeAsyncTask(&x2, [&](auto&&) { c = true; }));

  EXPECT_FALSE(a);
  EXPECT_FALSE(b);

  x1.run();
  EXPECT_TRUE(a);
  EXPECT_FALSE(b);
  EXPECT_FALSE(c);

  x1.run();
  EXPECT_TRUE(b);
  EXPECT_FALSE(c);

  x2.run();
  EXPECT_TRUE(c);
}

TEST(Via, allowInline) {
  ManualExecutor x1, x2;
  bool a = false, b = false, c = false, d = false, e = false, f = false,
       g = false, h = false, i = false, j = false, k = false, l = false,
       m = false, n = false, o = false, p = false, q = false, r = false;
  via(&x1)
      .thenValue([&](auto&&) { a = true; })
      .thenTryInline([&](auto&&) { b = true; })
      .thenTry([&](auto&&) { c = true; })
      .via(&x2)
      .thenTryInline([&](auto&&) { d = true; })
      .thenValue([&](auto&&) {
        e = true;
        return via(&x2).thenValue([&](auto&&) { f = true; });
      })
      .thenErrorInline(tag_t<std::exception>{}, [](auto&&) {})
      .thenValueInline([&](auto&&) { g = true; })
      .thenValue([&](auto&&) {
        h = true;
        return via(&x1).thenValue([&](auto&&) { i = true; });
      })
      .thenValueInline([&](auto&&) { j = true; })
      .semi()
      .deferValue([&](auto&&) { k = true; })
      .via(&x2)
      .thenValueInline([&](auto&&) { l = true; })
      .semi()
      .deferValue([&](auto&&) { m = true; })
      .via(&x1)
      .thenValue([&](auto&&) { n = true; })
      .semi()
      .deferValue([&](auto&&) { o = true; })
      .deferValue([&](auto&&) { p = true; })
      .via(&x1)
      .semi()
      .deferValue([&](auto&&) { q = true; })
      .deferValue([&](auto&&) { r = true; })
      .via(&x2);

  EXPECT_FALSE(a);
  EXPECT_FALSE(b);

  // Expect b to be satisfied inline with the task x1
  x1.run();
  EXPECT_TRUE(a);
  EXPECT_TRUE(b);
  EXPECT_FALSE(c);

  x1.run();
  EXPECT_TRUE(c);
  EXPECT_FALSE(d);

  // Demonstrate that the executor transition did not allow inline execution
  x2.run();
  EXPECT_TRUE(d);
  EXPECT_FALSE(e);

  x2.run();
  EXPECT_TRUE(e);
  EXPECT_FALSE(f);
  EXPECT_FALSE(g);

  // Completing nested continuation should satisfy inline continuation
  x2.run();
  EXPECT_TRUE(f);
  EXPECT_TRUE(g);
  EXPECT_FALSE(h);

  x2.run();
  EXPECT_TRUE(h);
  EXPECT_FALSE(i);
  EXPECT_FALSE(j);

  // Nested continuation on different executor should not complete next entry
  // inline
  x1.run();
  EXPECT_TRUE(i);
  EXPECT_FALSE(j);

  // Defer should run on x1 and therefore not inline
  // Subsequent deferred work is run on x1 and hence not inlined.
  x2.run();
  EXPECT_TRUE(j);
  EXPECT_TRUE(k);
  EXPECT_TRUE(l);
  EXPECT_FALSE(m);

  // Complete the deferred task
  x1.run();
  EXPECT_TRUE(m);
  EXPECT_FALSE(n);

  // Here defer and the above thenValue are both on x1, defer should be
  // inline
  x1.run();
  EXPECT_TRUE(n);
  EXPECT_TRUE(o);
  EXPECT_TRUE(p);
  EXPECT_FALSE(q);

  // Change of executor in deferred executor so now run x2 to complete
  x2.run();
  EXPECT_TRUE(q);
  EXPECT_TRUE(r);
}

#ifndef __APPLE__ // TODO #7372389
/// Simple executor that does work in another thread
class ThreadExecutor : public Executor {
  folly::MPMCQueue<Func> funcs;
  std::atomic<bool> done{false};
  std::thread worker;
  folly::Baton<> baton;

  void work() {
    baton.post();
    Func fn;
    while (!done) {
      while (!funcs.isEmpty()) {
        funcs.blockingRead(fn);
        fn();
      }
    }
  }

 public:
  explicit ThreadExecutor(size_t n = 1024) : funcs(n) {
    worker = std::thread(std::bind(&ThreadExecutor::work, this));
  }

  ~ThreadExecutor() override {
    done = true;
    funcs.write([] {});
    worker.join();
  }

  void add(Func fn) override { funcs.blockingWrite(std::move(fn)); }

  void waitForStartup() { baton.wait(); }
};

TEST(Via, viaThenGetWasRacy) {
  ThreadExecutor x;
  std::unique_ptr<int> val =
      folly::via(&x)
          .thenValue([](auto&&) { return std::make_unique<int>(42); })
          .get();
  ASSERT_TRUE(!!val);
  EXPECT_EQ(42, *val);
}

TEST(Via, callbackRace) {
  ThreadExecutor x;

  auto fn = [&x] {
    auto promises = std::make_shared<std::vector<Promise<Unit>>>(4);
    std::vector<Future<Unit>> futures;

    for (auto& p : *promises) {
      futures.emplace_back(p.getFuture().via(&x).thenTry([](Try<Unit>&&) {}));
    }

    x.waitForStartup();
    x.add([promises] {
      for (auto& p : *promises) {
        p.setValue();
      }
    });

    return collectAll(futures);
  };

  fn().wait();
}
#endif

class DummyDrivableExecutor : public DrivableExecutor {
 public:
  void add(Func /* f */) override {}
  void drive() override { ran = true; }
  bool ran{false};
};

TEST(Via, getVia) {
  {
    // non-void
    ManualExecutor x;
    auto f = via(&x).thenValue([](auto&&) { return true; });
    EXPECT_TRUE(std::move(f).getVia(&x));
  }

  {
    // void
    ManualExecutor x;
    auto f = via(&x).then();
    std::move(f).getVia(&x);
  }

  {
    DummyDrivableExecutor x;
    auto f = makeFuture(true);
    EXPECT_TRUE(std::move(f).getVia(&x));
    EXPECT_FALSE(x.ran);
  }
}

TEST(Via, SimpleTimedGetVia) {
  TimedDrivableExecutor e2;
  Promise<folly::Unit> p;
  auto f = p.getFuture();
  EXPECT_THROW(
      std::move(f).getVia(&e2, std::chrono::seconds(1)), FutureTimeout);
}

TEST(Via, getTryVia) {
  {
    // non-void
    ManualExecutor x;
    auto f = via(&x).thenValue([](auto&&) { return 23; });
    EXPECT_FALSE(f.isReady());
    EXPECT_EQ(23, std::move(f).getTryVia(&x).value());
  }

  {
    // void
    ManualExecutor x;
    auto f = via(&x).then();
    EXPECT_FALSE(f.isReady());
    auto t = std::move(f).getTryVia(&x);
    EXPECT_TRUE(t.hasValue());
  }

  {
    DummyDrivableExecutor x;
    auto f = makeFuture(23);
    EXPECT_EQ(23, std::move(f).getTryVia(&x).value());
    EXPECT_FALSE(x.ran);
  }
}

TEST(Via, SimpleTimedGetTryVia) {
  TimedDrivableExecutor e2;
  Promise<folly::Unit> p;
  auto f = p.getFuture();
  EXPECT_THROW(
      std::move(f).getTryVia(&e2, std::chrono::seconds(1)), FutureTimeout);
}

TEST(Via, waitVia) {
  {
    ManualExecutor x;
    auto f = via(&x).then();
    EXPECT_FALSE(f.isReady());
    f.waitVia(&x);
    EXPECT_TRUE(f.isReady());
  }

  {
    // try rvalue as well
    ManualExecutor x;
    auto f = via(&x).then().waitVia(&x);
    EXPECT_TRUE(f.isReady());
  }

  {
    DummyDrivableExecutor x;
    makeFuture(true).waitVia(&x);
    EXPECT_FALSE(x.ran);
  }
}

TEST(Via, viaRaces) {
  ManualExecutor x;
  Promise<Unit> p;
  auto tid = std::this_thread::get_id();
  bool done = false;

  std::thread t1([&] {
    p.getFuture()
        .via(&x)
        .thenTry(
            [&](Try<Unit>&&) { EXPECT_EQ(tid, std::this_thread::get_id()); })
        .thenTry(
            [&](Try<Unit>&&) { EXPECT_EQ(tid, std::this_thread::get_id()); })
        .thenTry([&](Try<Unit>&&) { done = true; });
  });

  std::thread t2([&] { p.setValue(); });

  while (!done) {
    x.run();
  }
  t1.join();
  t2.join();
}

TEST(Via, viaDummyExecutorFutureSetValueFirst) {
  // The callback object will get destroyed when passed to the executor.

  // A promise will be captured by the callback lambda so we can observe that
  // it will be destroyed.
  Promise<Unit> captured_promise;
  auto captured_promise_future = captured_promise.getFuture();

  DummyDrivableExecutor x;
  auto future = makeFuture().via(&x).thenValue(
      [c = std::move(captured_promise)](auto&&) { return 42; });

  EXPECT_THROW(std::move(future).get(std::chrono::seconds(5)), BrokenPromise);
  EXPECT_THROW(
      std::move(captured_promise_future).get(std::chrono::seconds(5)),
      BrokenPromise);
}

TEST(Via, viaDummyExecutorFutureSetCallbackFirst) {
  // The callback object will get destroyed when passed to the executor.

  // A promise will be captured by the callback lambda so we can observe that
  // it will be destroyed.
  Promise<Unit> captured_promise;
  auto captured_promise_future = captured_promise.getFuture();

  DummyDrivableExecutor x;
  Promise<Unit> trigger;
  auto future = trigger.getFuture().via(&x).thenValue(
      [c = std::move(captured_promise)](auto&&) { return 42; });
  trigger.setValue();

  EXPECT_THROW(std::move(future).get(std::chrono::seconds(5)), BrokenPromise);
  EXPECT_THROW(
      std::move(captured_promise_future).get(std::chrono::seconds(5)),
      BrokenPromise);
}

TEST(Via, viaExecutorDiscardsTaskFutureSetValueFirst) {
  // The callback object will get destroyed when the ManualExecutor runs out
  // of scope.

  // A promise will be captured by the callback lambda so we can observe that
  // it will be destroyed.
  Promise<Unit> captured_promise;
  auto captured_promise_future = captured_promise.getFuture();

  Optional<SemiFuture<int>> future;
  {
    ManualExecutor x;
    future =
        makeFuture()
            .via(&x)
            .thenValue([c = std::move(captured_promise)](auto&&) { return 42; })
            .semi();
    x.clear();
  }

  EXPECT_THROW(std::move(*future).get(std::chrono::seconds(5)), BrokenPromise);
  EXPECT_THROW(
      std::move(captured_promise_future).get(std::chrono::seconds(5)),
      BrokenPromise);
}

TEST(Via, viaExecutorDiscardsTaskFutureSetCallbackFirst) {
  // The callback object will get destroyed when the ManualExecutor runs out
  // of scope.

  // A promise will be captured by the callback lambda so we can observe that
  // it will be destroyed.
  Promise<Unit> captured_promise;
  auto captured_promise_future = captured_promise.getFuture();

  Optional<SemiFuture<int>> future;
  {
    ManualExecutor x;
    Promise<Unit> trigger;
    future =
        trigger.getFuture()
            .via(&x)
            .thenValue([c = std::move(captured_promise)](auto&&) { return 42; })
            .semi();
    trigger.setValue();
    x.clear();
  }

  EXPECT_THROW(std::move(*future).get(std::chrono::seconds(5)), BrokenPromise);
  EXPECT_THROW(
      std::move(captured_promise_future).get(std::chrono::seconds(5)),
      BrokenPromise);
}

TEST(ViaFunc, liftsVoid) {
  ManualExecutor x;
  int count = 0;
  Future<Unit> f = via(&x, [&] { count++; });

  EXPECT_EQ(0, count);
  x.run();
  EXPECT_EQ(1, count);
}

TEST(ViaFunc, value) {
  ManualExecutor x;
  EXPECT_EQ(42, via(&x, [] { return 42; }).getVia(&x));
}

TEST(ViaFunc, exception) {
  ManualExecutor x;
  EXPECT_THROW(
      via(&x, []() -> int { throw std::runtime_error("expected"); }).getVia(&x),
      std::runtime_error);
}

TEST(ViaFunc, future) {
  ManualExecutor x;
  EXPECT_EQ(42, via(&x, [] { return makeFuture(42); }).getVia(&x));
}

TEST(ViaFunc, semiFuture) {
  ManualExecutor x;
  EXPECT_EQ(42, via(&x, [] { return makeSemiFuture(42); }).getVia(&x));
}

TEST(ViaFunc, voidFuture) {
  ManualExecutor x;
  int count = 0;
  via(&x, [&] { count++; }).getVia(&x);
  EXPECT_EQ(1, count);
}

TEST(ViaFunc, isSticky) {
  ManualExecutor x;
  int count = 0;

  auto f = via(&x, [&] { count++; });
  x.run();

  std::move(f).thenValue([&](auto&&) { count++; });
  EXPECT_EQ(1, count);
  x.run();
  EXPECT_EQ(2, count);
}

TEST(ViaFunc, moveOnly) {
  ManualExecutor x;
  auto intp = std::make_unique<int>(42);

  EXPECT_EQ(42, via(&x, [intp = std::move(intp)] { return *intp; }).getVia(&x));
}

TEST(ViaFunc, valueKeepAlive) {
  ManualExecutor x;
  EXPECT_EQ(42, via(getKeepAliveToken(&x), [] { return 42; }).getVia(&x));
}

TEST(ViaFunc, thenValueKeepAlive) {
  ManualExecutor x;
  EXPECT_EQ(
      42,
      via(getKeepAliveToken(&x))
          .thenValue([](auto&&) { return 42; })
          .getVia(&x));
}
