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
#include <future>

#include "folly/wangle/Executor.h"
#include "folly/wangle/ManualExecutor.h"
#include "folly/wangle/ThreadGate.h"
#include "folly/wangle/GenericThreadGate.h"

using namespace folly::wangle;
using std::make_shared;
using std::shared_ptr;
using std::thread;
using std::vector;

struct ManualWaiter {
  explicit ManualWaiter(shared_ptr<ManualExecutor> ex) : ex(ex) {}

  void makeProgress() {
    ex->wait();
    ex->run();
  }

  shared_ptr<ManualExecutor> ex;
};

struct GenericThreadGateFixture : public testing::Test {
  GenericThreadGateFixture() :
    westExecutor(new ManualExecutor),
    eastExecutor(new ManualExecutor),
    waiter(new ManualWaiter(westExecutor)),
    tg(westExecutor, eastExecutor, waiter),
    done(false)
  {
    t = thread([=] {
      ManualWaiter eastWaiter(eastExecutor);
      while (!done)
        eastWaiter.makeProgress();
    });
  }

  ~GenericThreadGateFixture() {
    done = true;
    tg.gate<void>([] { return makeFuture(); });
    t.join();
  }

  shared_ptr<ManualExecutor> westExecutor;
  shared_ptr<ManualExecutor> eastExecutor;
  shared_ptr<ManualWaiter> waiter;
  GenericThreadGate<
    shared_ptr<ManualExecutor>,
    shared_ptr<ManualExecutor>,
    shared_ptr<ManualWaiter>> tg;
  bool done;
  thread t;
};

TEST_F(GenericThreadGateFixture, gate_and_wait) {
  auto f = tg.gate<void>([] { return makeFuture(); });
  EXPECT_FALSE(f.isReady());

  tg.waitFor(f);
  EXPECT_TRUE(f.isReady());
}

TEST_F(GenericThreadGateFixture, gate_many) {
  vector<Future<void>> fs;
  int n = 10;

  for (int i = 0; i < n; i++)
    fs.push_back(tg.gate<void>([&] { return makeFuture(); }));

  for (auto& f : fs)
    EXPECT_FALSE(f.isReady());

  auto all = whenAll(fs.begin(), fs.end());
  tg.waitFor(all);
}

TEST_F(GenericThreadGateFixture, gate_alternating) {
  vector<Promise<void>> ps(10);
  vector<Future<void>> fs;
  size_t count = 0;

  for (auto& p : ps) {
    auto* pp = &p;
    auto f = tg.gate<void>([=] { return pp->getFuture(); });

    // abuse the thread gate to do our dirty work in the other thread
    tg.gate<void>([=] { pp->setValue(); return makeFuture(); });

    fs.push_back(f.then([&](Try<void>&&) { count++; }));
  }

  for (auto& f : fs)
    EXPECT_FALSE(f.isReady());
  EXPECT_EQ(0, count);

  auto all = whenAll(fs.begin(), fs.end());
  tg.waitFor(all);

  EXPECT_EQ(ps.size(), count);
}

TEST(GenericThreadGate, noWaiter) {
  auto west = make_shared<ManualExecutor>();
  auto east = make_shared<ManualExecutor>();
  Promise<void> p;
  auto dummyFuture = p.getFuture();

  GenericThreadGate<ManualExecutor*, ManualExecutor*>
    tg(west.get(), east.get());

  EXPECT_THROW(tg.waitFor(dummyFuture), std::logic_error);
}

TEST_F(GenericThreadGateFixture, gate_with_promise) {
  Promise<int> p;

  auto westId = std::this_thread::get_id();
  bool westThenCalled = false;
  auto f = p.getFuture().then(
    [westId, &westThenCalled](Try<int>&& t) {
      EXPECT_EQ(t.value(), 1);
      EXPECT_EQ(std::this_thread::get_id(), westId);
      westThenCalled = true;
      return t.value();
    });

  bool eastPromiseMade = false;
  auto thread = std::thread([&p, &eastPromiseMade, this]() {
    EXPECT_NE(t.get_id(), std::this_thread::get_id());
    // South thread != west thread. p gets set in west thread.
    tg.gate<int>([&p, &eastPromiseMade, this] {
                   EXPECT_EQ(t.get_id(), std::this_thread::get_id());
                   Promise<int> eastPromise;
                   auto eastFuture = eastPromise.getFuture();
                   eastPromise.setValue(1);
                   eastPromiseMade = true;
                   return eastFuture;
                 },
                 std::move(p));
    });

  tg.waitFor(f);
  EXPECT_TRUE(westThenCalled);
  EXPECT_TRUE(eastPromiseMade);
  EXPECT_EQ(f.value(), 1);
  thread.join();
}
