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

#include <folly/executors/GlobalExecutor.h>

#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/executors/IOExecutor.h>
#include <folly/executors/IOThreadPoolExecutor.h>
#include <folly/portability/GTest.h>
#include <folly/synchronization/Baton.h>

TEST(GlobalCPUExecutorTest, CPUThreadCountFlagSet) {
  gflags::FlagSaver flagsaver;

  FLAGS_folly_global_cpu_executor_threads = 100;
  auto cpu_threadpool = dynamic_cast<folly::CPUThreadPoolExecutor*>(
      folly::getGlobalCPUExecutor().get());

  EXPECT_EQ(cpu_threadpool->numThreads(), 100);
}
