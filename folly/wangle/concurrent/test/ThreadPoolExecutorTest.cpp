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

#include <folly/wangle/concurrent/FutureExecutor.h>
#include <folly/wangle/concurrent/ThreadPoolExecutor.h>
#include <folly/wangle/concurrent/CPUThreadPoolExecutor.h>
#include <folly/wangle/concurrent/IOThreadPoolExecutor.h>
#include <glog/logging.h>
#include <gtest/gtest.h>

using namespace folly;
using namespace folly::wangle;
using namespace std::chrono;

static folly::Func burnMs(uint64_t ms) {
  return [ms]() { std::this_thread::sleep_for(milliseconds(ms)); };
}

template <class TPE>
static void basic() {
  // Create and destroy
  TPE tpe(10);
}

TEST(ThreadPoolExecutorTest, CPUBasic) {
  basic<CPUThreadPoolExecutor>();
}

TEST(IOThreadPoolExecutorTest, IOBasic) {
  basic<IOThreadPoolExecutor>();
}

template <class TPE>
static void resize() {
  TPE tpe(100);
  EXPECT_EQ(100, tpe.numThreads());
  tpe.setNumThreads(50);
  EXPECT_EQ(50, tpe.numThreads());
  tpe.setNumThreads(150);
  EXPECT_EQ(150, tpe.numThreads());
}

TEST(ThreadPoolExecutorTest, CPUResize) {
  resize<CPUThreadPoolExecutor>();
}

TEST(ThreadPoolExecutorTest, IOResize) {
  resize<IOThreadPoolExecutor>();
}

template <class TPE>
static void stop() {
  TPE tpe(1);
  std::atomic<int> completed(0);
  auto f = [&](){
    burnMs(10)();
    completed++;
  };
  for (int i = 0; i < 1000; i++) {
    tpe.add(f);
  }
  tpe.stop();
  EXPECT_GT(1000, completed);
}

// IOThreadPoolExecutor's stop() behaves like join(). Outstanding tasks belong
// to the event base, will be executed upon its destruction, and cannot be
// taken back.
template <>
void stop<IOThreadPoolExecutor>() {
  IOThreadPoolExecutor tpe(1);
  std::atomic<int> completed(0);
  auto f = [&](){
    burnMs(10)();
    completed++;
  };
  for (int i = 0; i < 10; i++) {
    tpe.add(f);
  }
  tpe.stop();
  EXPECT_EQ(10, completed);
}

TEST(ThreadPoolExecutorTest, CPUStop) {
  stop<CPUThreadPoolExecutor>();
}

TEST(ThreadPoolExecutorTest, IOStop) {
  stop<IOThreadPoolExecutor>();
}

template <class TPE>
static void join() {
  TPE tpe(10);
  std::atomic<int> completed(0);
  auto f = [&](){
    burnMs(1)();
    completed++;
  };
  for (int i = 0; i < 1000; i++) {
    tpe.add(f);
  }
  tpe.join();
  EXPECT_EQ(1000, completed);
}

TEST(ThreadPoolExecutorTest, CPUJoin) {
  join<CPUThreadPoolExecutor>();
}

TEST(ThreadPoolExecutorTest, IOJoin) {
  join<IOThreadPoolExecutor>();
}

template <class TPE>
static void resizeUnderLoad() {
  TPE tpe(10);
  std::atomic<int> completed(0);
  auto f = [&](){
    burnMs(1)();
    completed++;
  };
  for (int i = 0; i < 1000; i++) {
    tpe.add(f);
  }
  tpe.setNumThreads(5);
  tpe.setNumThreads(15);
  tpe.join();
  EXPECT_EQ(1000, completed);
}

TEST(ThreadPoolExecutorTest, CPUResizeUnderLoad) {
  resizeUnderLoad<CPUThreadPoolExecutor>();
}

TEST(ThreadPoolExecutorTest, IOResizeUnderLoad) {
  resizeUnderLoad<IOThreadPoolExecutor>();
}

template <class TPE>
static void poolStats() {
  folly::Baton<> startBaton, endBaton;
  TPE tpe(1);
  auto stats = tpe.getPoolStats();
  EXPECT_EQ(1, stats.threadCount);
  EXPECT_EQ(1, stats.idleThreadCount);
  EXPECT_EQ(0, stats.activeThreadCount);
  EXPECT_EQ(0, stats.pendingTaskCount);
  EXPECT_EQ(0, stats.totalTaskCount);
  tpe.add([&](){ startBaton.post(); endBaton.wait(); });
  tpe.add([&](){});
  startBaton.wait();
  stats = tpe.getPoolStats();
  EXPECT_EQ(1, stats.threadCount);
  EXPECT_EQ(0, stats.idleThreadCount);
  EXPECT_EQ(1, stats.activeThreadCount);
  EXPECT_EQ(1, stats.pendingTaskCount);
  EXPECT_EQ(2, stats.totalTaskCount);
  endBaton.post();
}

TEST(ThreadPoolExecutorTest, CPUPoolStats) {
  poolStats<CPUThreadPoolExecutor>();
}

TEST(ThreadPoolExecutorTest, IOPoolStats) {
  poolStats<IOThreadPoolExecutor>();
}

template <class TPE>
static void taskStats() {
  TPE tpe(1);
  std::atomic<int> c(0);
  auto s = tpe.subscribeToTaskStats(
      Observer<ThreadPoolExecutor::TaskStats>::create(
          [&](ThreadPoolExecutor::TaskStats stats) {
        int i = c++;
        EXPECT_LT(milliseconds(0), stats.runTime);
        if (i == 1) {
          EXPECT_LT(milliseconds(0), stats.waitTime);
        }
      }));
  tpe.add(burnMs(10));
  tpe.add(burnMs(10));
  tpe.join();
  EXPECT_EQ(2, c);
}

TEST(ThreadPoolExecutorTest, CPUTaskStats) {
  taskStats<CPUThreadPoolExecutor>();
}

TEST(ThreadPoolExecutorTest, IOTaskStats) {
  taskStats<IOThreadPoolExecutor>();
}

template <class TPE>
static void expiration() {
  TPE tpe(1);
  std::atomic<int> statCbCount(0);
  auto s = tpe.subscribeToTaskStats(
      Observer<ThreadPoolExecutor::TaskStats>::create(
          [&](ThreadPoolExecutor::TaskStats stats) {
        int i = statCbCount++;
        if (i == 0) {
          EXPECT_FALSE(stats.expired);
        } else if (i == 1) {
          EXPECT_TRUE(stats.expired);
        } else {
          FAIL();
        }
      }));
  std::atomic<int> expireCbCount(0);
  auto expireCb = [&] () { expireCbCount++; };
  tpe.add(burnMs(10), seconds(60), expireCb);
  tpe.add(burnMs(10), milliseconds(10), expireCb);
  tpe.join();
  EXPECT_EQ(2, statCbCount);
  EXPECT_EQ(1, expireCbCount);
}

TEST(ThreadPoolExecutorTest, CPUExpiration) {
  expiration<CPUThreadPoolExecutor>();
}

TEST(ThreadPoolExecutorTest, IOExpiration) {
  expiration<IOThreadPoolExecutor>();
}

template <typename TPE>
static void futureExecutor() {
  FutureExecutor<TPE> fe(2);
  std::atomic<int> c{0};
  fe.addFuture([] () { return makeFuture<int>(42); }).then(
    [&] (Try<int>&& t) {
      c++;
      EXPECT_EQ(42, t.value());
    });
  fe.addFuture([] () { return 100; }).then(
    [&] (Try<int>&& t) {
      c++;
      EXPECT_EQ(100, t.value());
    });
  fe.addFuture([] () { return makeFuture(); }).then(
    [&] (Try<void>&& t) {
      c++;
      EXPECT_NO_THROW(t.value());
    });
  fe.addFuture([] () { return; }).then(
    [&] (Try<void>&& t) {
      c++;
      EXPECT_NO_THROW(t.value());
    });
  fe.addFuture([] () { throw std::runtime_error("oops"); }).then(
    [&] (Try<void>&& t) {
      c++;
      EXPECT_THROW(t.value(), std::runtime_error);
    });
  // Test doing actual async work
  folly::Baton<> baton;
  fe.addFuture([&] () {
    auto p = std::make_shared<Promise<int>>();
    std::thread t([p](){
      burnMs(10)();
      p->setValue(42);
    });
    t.detach();
    return p->getFuture();
  }).then([&] (Try<int>&& t) {
    EXPECT_EQ(42, t.value());
    c++;
    baton.post();
  });
  baton.wait();
  fe.join();
  EXPECT_EQ(6, c);
}

TEST(ThreadPoolExecutorTest, CPUFuturePool) {
  futureExecutor<CPUThreadPoolExecutor>();
}

TEST(ThreadPoolExecutorTest, IOFuturePool) {
  futureExecutor<IOThreadPoolExecutor>();
}

TEST(ThreadPoolExecutorTest, PriorityPreemptionTest) {
  bool tookLopri = false;
  auto completed = 0;
  auto hipri = [&] {
    EXPECT_FALSE(tookLopri);
    completed++;
  };
  auto lopri = [&] {
    tookLopri = true;
    completed++;
  };
  CPUThreadPoolExecutor pool(0, 2);
  for (int i = 0; i < 50; i++) {
    pool.add(lopri, 0);
  }
  for (int i = 0; i < 50; i++) {
    pool.add(hipri, 1);
  }
  pool.setNumThreads(1);
  pool.join();
  EXPECT_EQ(100, completed);
}

class TestObserver : public ThreadPoolExecutor::Observer {
 public:
  void threadStarted(ThreadPoolExecutor::ThreadHandle*) {
    threads_++;
  }
  void threadStopped(ThreadPoolExecutor::ThreadHandle*) {
    threads_--;
  }
  void threadPreviouslyStarted(ThreadPoolExecutor::ThreadHandle*) {
    threads_++;
  }
  void threadNotYetStopped(ThreadPoolExecutor::ThreadHandle*) {
    threads_--;
  }
  void checkCalls() {
    ASSERT_EQ(threads_, 0);
  }
 private:
  std::atomic<int> threads_{0};
};

TEST(ThreadPoolExecutorTest, IOObserver) {
  auto observer = std::make_shared<TestObserver>();

  {
    IOThreadPoolExecutor exe(10);
    exe.addObserver(observer);
    exe.setNumThreads(3);
    exe.setNumThreads(0);
    exe.setNumThreads(7);
    exe.removeObserver(observer);
    exe.setNumThreads(10);
  }

  observer->checkCalls();
}

TEST(ThreadPoolExecutorTest, CPUObserver) {
  auto observer = std::make_shared<TestObserver>();

  {
    CPUThreadPoolExecutor exe(10);
    exe.addObserver(observer);
    exe.setNumThreads(3);
    exe.setNumThreads(0);
    exe.setNumThreads(7);
    exe.removeObserver(observer);
    exe.setNumThreads(10);
  }

  observer->checkCalls();
}
