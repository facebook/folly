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

#include <memory>
#include <mutex>
#include <queue>

#include <folly/executors/ManualExecutor.h>
#include <folly/futures/Future.h>
#include <folly/futures/Promise.h>
#include <folly/portability/GTest.h>

using namespace folly;

inline void popAndFulfillPromise(
    std::queue<std::shared_ptr<Promise<Unit>>>& ps,
    std::mutex& ps_mutex) {
  ps_mutex.lock();
  auto p = ps.front();
  ps.pop();
  ps_mutex.unlock();
  p->setValue();
}

inline std::function<Future<Unit>(void)> makeThunk(
    std::queue<std::shared_ptr<Promise<Unit>>>& ps,
    int& interrupt,
    std::mutex& ps_mutex) {
  return [&]() mutable {
    auto p = std::make_shared<Promise<Unit>>();
    p->setInterruptHandler(
        [&](exception_wrapper const& /* e */) { ++interrupt; });
    ps_mutex.lock();
    ps.push(p);
    ps_mutex.unlock();

    return p->getFuture();
  };
}

inline std::function<bool(void)> makePred(int& i) {
  return [&]() {
    bool res = i < 3;
    ++i;
    return res;
  };
}

template <class F>
inline void successTest(
    std::queue<std::shared_ptr<Promise<Unit>>>& ps,
    std::mutex& ps_mutex,
    F& thunk) {
  folly::ManualExecutor executor;
  int i = 0;
  bool complete = false;
  bool failure = false;

  auto pred = makePred(i);
  auto f = folly::whileDo(pred, thunk)
               .via(&executor)
               .thenValue([&](auto&&) mutable { complete = true; })
               .thenError(folly::tag_t<FutureException>{}, [&](auto&& /* e */) {
                 failure = true;
               });

  executor.drain();
  popAndFulfillPromise(ps, ps_mutex);
  EXPECT_FALSE(complete);
  EXPECT_FALSE(failure);

  executor.drain();
  popAndFulfillPromise(ps, ps_mutex);
  EXPECT_FALSE(complete);
  EXPECT_FALSE(failure);

  executor.drain();
  popAndFulfillPromise(ps, ps_mutex);
  executor.drain();
  EXPECT_TRUE(f.isReady());
  EXPECT_TRUE(complete);
  EXPECT_FALSE(failure);
}

TEST(WhileDo, success) {
  std::queue<std::shared_ptr<Promise<Unit>>> ps;
  std::mutex ps_mutex;
  int interrupt = 0;
  auto thunk = makeThunk(ps, interrupt, ps_mutex);
  successTest(ps, ps_mutex, thunk);
}

TEST(WhileDo, semiFutureSuccess) {
  std::queue<std::shared_ptr<Promise<Unit>>> ps;
  std::mutex ps_mutex;
  int interrupt = 0;
  auto thunk = [t = makeThunk(ps, interrupt, ps_mutex)]() {
    return t().semi();
  };
  successTest(ps, ps_mutex, thunk);
}

template <class F>
inline void failureTest(
    std::queue<std::shared_ptr<Promise<Unit>>>& ps,
    std::mutex& ps_mutex,
    F& thunk) {
  folly::ManualExecutor executor;
  int i = 0;
  bool complete = false;
  bool failure = false;

  auto pred = makePred(i);
  auto f = folly::whileDo(pred, thunk)
               .via(&executor)
               .thenValue([&](auto&&) mutable { complete = true; })
               .thenError(folly::tag_t<FutureException>{}, [&](auto&& /* e */) {
                 failure = true;
               });

  executor.drain();
  popAndFulfillPromise(ps, ps_mutex);
  executor.drain();
  EXPECT_FALSE(complete);
  EXPECT_FALSE(failure);

  ps_mutex.lock();
  auto p2 = ps.front();
  ps.pop();
  ps_mutex.unlock();
  FutureException eggs("eggs");
  p2->setException(eggs);

  executor.drain();
  EXPECT_TRUE(f.isReady());
  EXPECT_FALSE(complete);
  EXPECT_TRUE(failure);
}

TEST(WhileDo, failure) {
  std::queue<std::shared_ptr<Promise<Unit>>> ps;
  std::mutex ps_mutex;
  int interrupt = 0;
  auto thunk = makeThunk(ps, interrupt, ps_mutex);
  failureTest(ps, ps_mutex, thunk);
}

TEST(WhileDo, semiFutureFailure) {
  std::queue<std::shared_ptr<Promise<Unit>>> ps;
  std::mutex ps_mutex;
  int interrupt = 0;
  auto thunk = [t = makeThunk(ps, interrupt, ps_mutex)]() {
    return t().semi();
  };
  failureTest(ps, ps_mutex, thunk);
}

template <class F>
inline void interruptTest(
    std::queue<std::shared_ptr<Promise<Unit>>>& ps,
    std::mutex& ps_mutex,
    int& interrupt,
    F& thunk) {
  folly::ManualExecutor executor;
  bool complete = false;
  bool failure = false;

  int i = 0;
  auto pred = makePred(i);
  auto f = folly::whileDo(pred, thunk)
               .via(&executor)
               .thenValue([&](auto&&) mutable { complete = true; })
               .thenError(folly::tag_t<FutureException>{}, [&](auto&& /* e */) {
                 failure = true;
               });

  executor.drain();
  EXPECT_EQ(0, interrupt);

  FutureException eggs("eggs");
  f.raise(eggs);

  executor.drain();
  for (int j = 1; j <= 3; ++j) {
    EXPECT_EQ(1, interrupt);
    popAndFulfillPromise(ps, ps_mutex);
    executor.drain();
  }
}

TEST(WhileDo, interrupt) {
  std::queue<std::shared_ptr<Promise<Unit>>> ps;
  std::mutex ps_mutex;
  int interrupt = 0;
  auto thunk = makeThunk(ps, interrupt, ps_mutex);
  interruptTest(ps, ps_mutex, interrupt, thunk);
}

TEST(WhileDo, semiFutureInterrupt) {
  std::queue<std::shared_ptr<Promise<Unit>>> ps;
  std::mutex ps_mutex;
  int interrupt = 0;
  auto thunk = [t = makeThunk(ps, interrupt, ps_mutex)]() {
    return t().semi();
  };
  interruptTest(ps, ps_mutex, interrupt, thunk);
}
