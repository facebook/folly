/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <folly/CancellationToken.h>

#include <folly/Optional.h>
#include <folly/portability/GTest.h>
#include <folly/synchronization/Baton.h>
#include <chrono>
#include <thread>

using namespace folly;
using namespace std::literals::chrono_literals;

TEST(CancellationTokenTest, DefaultCancellationTokenIsNotCancellable) {
  CancellationToken t;
  EXPECT_FALSE(t.isCancellationRequested());
  EXPECT_FALSE(t.canBeCancelled());

  CancellationToken tCopy = t;
  EXPECT_FALSE(tCopy.isCancellationRequested());
  EXPECT_FALSE(tCopy.canBeCancelled());

  CancellationToken tMoved = std::move(t);
  EXPECT_FALSE(tMoved.isCancellationRequested());
  EXPECT_FALSE(tMoved.canBeCancelled());
}

TEST(CancellationTokenTest, Polling) {
  CancellationSource src;
  EXPECT_FALSE(src.isCancellationRequested());
  EXPECT_TRUE(src.canBeCancelled());

  CancellationToken token = src.getToken();
  EXPECT_FALSE(token.isCancellationRequested());
  EXPECT_TRUE(token.canBeCancelled());

  CancellationToken tokenCopy = token;
  EXPECT_FALSE(tokenCopy.isCancellationRequested());
  EXPECT_TRUE(tokenCopy.canBeCancelled());

  src.requestCancellation();
  EXPECT_TRUE(token.isCancellationRequested());
  EXPECT_TRUE(tokenCopy.isCancellationRequested());
}

TEST(CancellationTokenTest, MultiThreadedPolling) {
  CancellationSource src;

  std::thread t1{[t = src.getToken()] {
    while (!t.isCancellationRequested()) {
      std::this_thread::yield();
    }
  }};

  src.requestCancellation();

  t1.join();
}

TEST(CancellationTokenTest, TokenIsNotCancellableOnceLastSourceIsDestroyed) {
  CancellationToken token;
  {
    CancellationSource src;
    token = src.getToken();
    {
      CancellationSource srcCopy1;
      CancellationSource srcCopy2;
      EXPECT_TRUE(token.canBeCancelled());
    }
    EXPECT_TRUE(token.canBeCancelled());
  }
  EXPECT_FALSE(token.canBeCancelled());
}

TEST(
    CancellationTokenTest,
    TokenRemainsCancellableEvenOnceLastSourceIsDestroyed) {
  CancellationToken token;
  {
    CancellationSource src;
    token = src.getToken();
    {
      CancellationSource srcCopy1;
      CancellationSource srcCopy2;
      EXPECT_TRUE(token.canBeCancelled());
    }
    EXPECT_TRUE(token.canBeCancelled());
    src.requestCancellation();
  }
  EXPECT_TRUE(token.canBeCancelled());
  EXPECT_TRUE(token.isCancellationRequested());
}

TEST(CancellationTokenTest, CallbackRegistration) {
  CancellationSource src;

  bool callbackExecuted = false;
  CancellationCallback cb{src.getToken(), [&] { callbackExecuted = true; }};

  EXPECT_FALSE(callbackExecuted);

  src.requestCancellation();

  EXPECT_TRUE(callbackExecuted);
}

TEST(CancellationTokenTest, CallbackExecutesImmediatelyIfAlreadyCancelled) {
  CancellationSource src;
  src.requestCancellation();

  bool callbackExecuted = false;
  CancellationCallback cb{src.getToken(), [&] { callbackExecuted = true; }};

  EXPECT_TRUE(callbackExecuted);
}

TEST(CancellationTokenTest, CallbackShouldNotBeExecutedMultipleTimes) {
  CancellationSource src;

  int callbackExecutionCount = 0;
  CancellationCallback cb{src.getToken(), [&] { ++callbackExecutionCount; }};

  src.requestCancellation();

  EXPECT_EQ(1, callbackExecutionCount);

  src.requestCancellation();

  EXPECT_EQ(1, callbackExecutionCount);
}

TEST(CancellationTokenTest, RegisterMultipleCallbacks) {
  CancellationSource src;

  bool executed1 = false;
  CancellationCallback cb1{src.getToken(), [&] { executed1 = true; }};

  bool executed2 = false;
  CancellationCallback cb2{src.getToken(), [&] { executed2 = true; }};

  EXPECT_FALSE(executed1);
  EXPECT_FALSE(executed2);

  src.requestCancellation();

  EXPECT_TRUE(executed1);
  EXPECT_TRUE(executed2);
}

TEST(CancellationTokenTest, DeregisteredCallbacksDontExecute) {
  CancellationSource src;

  bool executed1 = false;
  bool executed2 = false;

  CancellationCallback cb1{src.getToken(), [&] { executed1 = true; }};
  {
    CancellationCallback cb2{src.getToken(), [&] { executed2 = true; }};
  }

  src.requestCancellation();

  EXPECT_TRUE(executed1);
  EXPECT_FALSE(executed2);
}

TEST(CancellationTokenTest, CallbackThatDeregistersItself) {
  CancellationSource src;
  // Check that this doesn't deadlock when a callback tries to deregister
  // itself from within the callback.
  folly::Optional<CancellationCallback> callback;
  callback.emplace(src.getToken(), [&] { callback.reset(); });
  src.requestCancellation();
}
TEST(CancellationTokenTest, ManyCallbacks) {
  // This test checks that the CancellationSource internal state is able to
  // grow to accommodate a large number of callbacks and that there are no
  // memory leaks when it's all eventually destroyed.
  CancellationSource src;
  auto addLotsOfCallbacksAndWait = [t = src.getToken()] {
    int counter = 0;
    std::vector<std::unique_ptr<CancellationCallback>> callbacks;
    for (int i = 0; i < 100; ++i) {
      callbacks.push_back(
          std::make_unique<CancellationCallback>(t, [&] { ++counter; }));
    }

    Baton<> baton;
    CancellationCallback cb{t, [&] { baton.post(); }};
    baton.wait();
  };

  std::thread t1{addLotsOfCallbacksAndWait};
  std::thread t2{addLotsOfCallbacksAndWait};
  std::thread t3{addLotsOfCallbacksAndWait};
  std::thread t4{addLotsOfCallbacksAndWait};

  src.requestCancellation();

  t1.join();
  t2.join();
  t3.join();
  t4.join();
}

TEST(CancellationTokenTest, ManyConcurrentCallbackAddRemove) {
  auto runTest = [](CancellationToken ct) {
    auto cb = [] { std::this_thread::sleep_for(1ms); };
    while (!ct.isCancellationRequested()) {
      CancellationCallback cb1{ct, cb};
      CancellationCallback cb2{ct, cb};
      CancellationCallback cb3{ct, cb};
      CancellationCallback cb5{ct, cb};
      CancellationCallback cb6{ct, cb};
      CancellationCallback cb7{ct, cb};
      CancellationCallback cb8{ct, cb};
    }
  };

  CancellationSource src;

  std::vector<std::thread> threads;
  for (int i = 0; i < 10; ++i) {
    threads.emplace_back([&, t = src.getToken()] { runTest(t); });
  }

  std::this_thread::sleep_for(1s);

  src.requestCancellation();

  for (auto& t : threads) {
    t.join();
  }
}

TEST(CancellationTokenTest, NonCancellableSource) {
  CancellationSource src = CancellationSource::invalid();
  CHECK(!src.canBeCancelled());
  CHECK(!src.isCancellationRequested());
  CHECK(!src.requestCancellation());
  CHECK(!src.isCancellationRequested());
  CHECK(!src.canBeCancelled());

  auto token = src.getToken();
  CHECK(!src.canBeCancelled());
  CHECK(!src.isCancellationRequested());
  CHECK(token == CancellationToken{});
}
