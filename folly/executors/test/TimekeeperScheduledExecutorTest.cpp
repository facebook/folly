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

#include <atomic>
#include <chrono>

#include <folly/executors/InlineExecutor.h>
#include <folly/executors/SerialExecutor.h>
#include <folly/executors/ThreadedExecutor.h>
#include <folly/executors/TimekeeperScheduledExecutor.h>
#include <folly/futures/ManualTimekeeper.h>
#include <folly/portability/GTest.h>
#include <folly/synchronization/Baton.h>

using folly::ManualTimekeeper;
using folly::TimekeeperScheduledExecutor;

namespace {
void simpleTest(std::unique_ptr<folly::Executor> const& parent) {
  auto tk = std::make_shared<ManualTimekeeper>();
  auto executor = TimekeeperScheduledExecutor::create(
      folly::getKeepAliveToken(parent.get()), [tk]() { return tk; });

  // Test add()
  constexpr int reps = 20;
  std::atomic<int> repsLeft(reps), sum(0);
  folly::Baton<> finished;
  int expectedSum = 0;
  for (int i = 0; i < reps; ++i) {
    executor->add([i, &sum, &repsLeft, &finished] {
      sum += i;
      if (--repsLeft == 0) {
        finished.post();
      }
    });
    EXPECT_EQ(tk->numScheduled(), 0);
    expectedSum += i;
  }
  finished.wait();
  EXPECT_EQ(sum, expectedSum);

  // Test scheduleAt()
  finished.reset();
  executor->scheduleAt(
      [&finished]() { finished.post(); }, tk->now() + std::chrono::seconds(2));
  EXPECT_EQ(tk->numScheduled(), 1);
  EXPECT_FALSE(finished.ready());
  tk->advance(std::chrono::seconds(1));
  EXPECT_EQ(tk->numScheduled(), 1);
  EXPECT_FALSE(finished.ready());
  tk->advance(std::chrono::seconds(1));
  EXPECT_EQ(tk->numScheduled(), 0);
  finished.wait();
}

TEST(TimekeeperScheduledExecutor, SimpleThreaded) {
  simpleTest(std::make_unique<folly::ThreadedExecutor>());
}

TEST(TimekeeperScheduledExecutor, SimpleInline) {
  simpleTest(std::make_unique<folly::InlineExecutor>());
}

TEST(TimekeeperScheduledExecutor, Afterlife) {
  auto grandparent = std::make_unique<folly::ThreadedExecutor>();
  auto parent =
      folly::SerialExecutor::create(getKeepAliveToken(grandparent.get()));
  auto executor = TimekeeperScheduledExecutor::create(
      folly::getKeepAliveToken(parent.get()));

  folly::Baton<> startBaton;
  executor->add([&startBaton] { startBaton.wait(); });

  constexpr int reps = 20;
  std::atomic<int> sum(0);
  int expectedSum = 0;
  for (int i = 0; i < reps; ++i) {
    executor->add([i, &sum] { sum += i; });
    expectedSum += i;
  }

  folly::Baton<> finishedBaton;
  executor->add([&finishedBaton] { finishedBaton.post(); });

  // drop our reference to TimekeeperScheduledExecutor
  executor.reset();
  // Verify no tasks have started yet
  EXPECT_EQ(sum, 0);

  // now kick off the tasks
  startBaton.post();

  // wait until last task has executed
  finishedBaton.wait();

  EXPECT_EQ(sum, expectedSum);
}

void RecursiveAddTest(std::unique_ptr<folly::Executor> const& grandparent) {
  auto parent =
      folly::SerialExecutor::create(getKeepAliveToken(grandparent.get()));
  auto executor = TimekeeperScheduledExecutor::create(
      folly::getKeepAliveToken(parent.get()));

  folly::Baton<> finishedBaton;
  int i = 0, sum = 0;
  std::function<void()> lambda = [&]() {
    if (i < 10) {
      executor->add(lambda);
    } else if (i < 12) {
      // Below we will post this lambda three times to the executor. When
      // executed, the lambda will re-post itself during the first ten
      // executions. Afterwards we do nothing twice (this else-branch), and
      // then on the 13th execution signal that we are finished.
    } else {
      finishedBaton.post();
      return;
    }
    sum += ++i;
  };

  executor->add(lambda);
  executor->add(lambda);
  executor->add(lambda);

  // wait until last task has executed
  finishedBaton.wait();

  EXPECT_EQ(sum, 78);
}

TEST(TimekeeperScheduledExecutor, RecursiveAdd) {
  RecursiveAddTest(std::make_unique<folly::ThreadedExecutor>());
}

TEST(TimekeeperScheduledExecutor, RecursiveAddInline) {
  RecursiveAddTest(std::make_unique<folly::InlineExecutor>());
}

TEST(TimekeeperScheduledExecutor, ExecutionThrows) {
  auto parent = std::make_unique<folly::ThreadedExecutor>();
  auto executor =
      TimekeeperScheduledExecutor::create(getKeepAliveToken(parent.get()));
  // An empty Func will throw std::bad_function_call when invoked,
  // but TimekeeperScheduledExecutor should catch that exception.
  executor->add(folly::Func{});
}

TEST(TimekeeperScheduledExecutor, NoTimekeeper) {
  auto parent = std::make_unique<folly::ThreadedExecutor>();
  // A TimekeeperScheduledExecutor that can't access its Timekeeper
  //   should throw the appropriate exception.
  auto executor = TimekeeperScheduledExecutor::create(
      getKeepAliveToken(parent.get()), []() { return nullptr; });
  executor->add([]() {});
  EXPECT_THROW(
      executor->schedule([]() {}, std::chrono::seconds(9)),
      folly::TimekeeperScheduledExecutorNoTimekeeper);
}

} // namespace
