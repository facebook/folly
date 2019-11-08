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

#include <folly/executors/ExecutorWithPriority.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/futures/Future.h>
#include <folly/portability/GTest.h>

using namespace folly;

TEST(ExecutorWithPriorityTest, addWithCorrectPriorityTest) {
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
  auto pool = std::make_shared<CPUThreadPoolExecutor>(
      0 /*numThreads*/, 2 /*numPriorities*/);
  {
    auto loPriExecutor = ExecutorWithPriority::create(
        getKeepAliveToken(pool.get()), Executor::LO_PRI);
    auto hiPriExecutor = ExecutorWithPriority::create(
        getKeepAliveToken(pool.get()), Executor::HI_PRI);
    for (int i = 0; i < 50; i++) {
      loPriExecutor->add(lopri);
    }
    for (int i = 0; i < 50; i++) {
      hiPriExecutor->add(hipri);
    }
    pool->setNumThreads(1);
  }
  pool->join();
  EXPECT_EQ(100, completed);
}

TEST(ExecutorWithPriorityTest, updatePriorityUsingCallback) {
  std::atomic<int8_t> curPri = -25;
  auto completed = 0;
  auto callback = [&curPri]() { return curPri += 1; };
  // priorities range from -25 to 25
  auto pool = std::make_shared<CPUThreadPoolExecutor>(
      0 /*numThreads*/, 51 /*numPriorities*/);
  {
    auto executorWithPriority = ExecutorWithPriority::createDynamic(
        getKeepAliveToken(pool.get()), callback);
    for (int i = 0; i < 50; i++) {
      auto task = [&completed, i]() {
        completed++;
        EXPECT_EQ(completed, 50 - i);
      };
      executorWithPriority->add(std::move(task));
    }
    pool->setNumThreads(1);
  }
  pool->join();
  EXPECT_EQ(50, completed);
  EXPECT_EQ(25, curPri);
}
