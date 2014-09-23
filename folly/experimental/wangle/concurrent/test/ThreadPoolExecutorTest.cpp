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

#include <folly/experimental/wangle/concurrent/ThreadPoolExecutor.h>
#include <folly/experimental/wangle/concurrent/CPUThreadPoolExecutor.h>
#include <folly/experimental/wangle/concurrent/IOThreadPoolExecutor.h>
#include <glog/logging.h>
#include <gtest/gtest.h>

using namespace folly::wangle;

static Func burnMs(uint64_t ms) {
  return [ms]() { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); };
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
  TPE tpe(10);
  std::atomic<int> completed(0);
  auto f = [&](){
    burnMs(1)();
    completed++;
  };
  for (int i = 0; i < 1000; i++) {
    tpe.add(f);
  }
  tpe.stop();
  EXPECT_GT(1000, completed);
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
  {
    TPE tpe(10);
    for (int i = 0; i < 20; i++) {
      tpe.add(burnMs(20));
    }
    burnMs(10)();
    auto stats = tpe.getPoolStats();
    EXPECT_EQ(10, stats.threadCount);
    EXPECT_EQ(0, stats.idleThreadCount);
    EXPECT_EQ(10, stats.activeThreadCount);
    EXPECT_EQ(10, stats.pendingTaskCount);
    EXPECT_EQ(20, stats.totalTaskCount);
  }

  {
    TPE tpe(10);
    for (int i = 0; i < 5; i++) {
      tpe.add(burnMs(20));
    }
    burnMs(10)();
    auto stats = tpe.getPoolStats();
    EXPECT_EQ(10, stats.threadCount);
    EXPECT_EQ(5, stats.idleThreadCount);
    EXPECT_EQ(5, stats.activeThreadCount);
    EXPECT_EQ(0, stats.pendingTaskCount);
    EXPECT_EQ(5, stats.totalTaskCount);
  }
}

TEST(ThreadPoolExecutorTest, CPUPoolStats) {
  poolStats<CPUThreadPoolExecutor>();
}

TEST(ThreadPoolExecutorTest, IOPoolStats) {
  poolStats<IOThreadPoolExecutor>();
}

template <class TPE>
static void taskStats() {
  TPE tpe(10);
  std::atomic<int> c(0);
  tpe.subscribeToTaskStats(Observer<ThreadPoolExecutor::TaskStats>::create(
      [&] (ThreadPoolExecutor::TaskStats stats) {
        int i = c++;
        if (i < 10) {
          EXPECT_GE(10000, stats.waitTime.count());
          EXPECT_LE(20000, stats.runTime.count());
        } else {
          EXPECT_LE(10000, stats.waitTime.count());
          EXPECT_LE(10000, stats.runTime.count());
        }
      }));
  for (int i = 0; i < 10; i++) {
    tpe.add(burnMs(20));
  }
  for (int i = 0; i < 10; i++) {
    tpe.add(burnMs(10));
  }
  tpe.join();
  EXPECT_EQ(20, c);
}

TEST(ThreadPoolExecutorTest, CPUTaskStats) {
  taskStats<CPUThreadPoolExecutor>();
}

TEST(ThreadPoolExecutorTest, IOTaskStats) {
  taskStats<IOThreadPoolExecutor>();
}
