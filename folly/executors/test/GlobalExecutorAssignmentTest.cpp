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

#include <folly/Singleton.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/executors/GlobalExecutor.h>
#include <folly/executors/IOExecutor.h>
#include <folly/executors/InlineExecutor.h>
#include <folly/portability/GTest.h>
#include <folly/synchronization/Baton.h>

using namespace folly;

TEST(GlobalExecutorAssignmentTest, GlobalCPUExecutorAsImmutable) {
  folly::Baton b;
  // The default CPU executor is a synchronous inline executor, lets verify
  // that work we add is executed
  auto count = 0;
  auto f = [&]() {
    count++;
    b.post();
  };

  {
    auto inlineExec = getCPUExecutor();
    EXPECT_EQ(
        dynamic_cast<folly::CPUThreadPoolExecutor*>(inlineExec.get()), nullptr);

    setCPUExecutorToGlobalCPUExecutor();

    auto cpuExec = getCPUExecutor();
    EXPECT_NE(
        dynamic_cast<folly::CPUThreadPoolExecutor*>(cpuExec.get()), nullptr);

    // Verify execution
    getCPUExecutor()->add(f);
    b.wait();
    EXPECT_EQ(1, count);
  }
}
