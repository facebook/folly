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

#include <folly/system/Pid.h>

#include <glog/logging.h>

#include <folly/portability/GTest.h>

TEST(PidTest, basic) {
  pid_t mypid = getpid();
  EXPECT_EQ(mypid, folly::get_cached_pid());
  EXPECT_EQ(getpid(), folly::get_cached_pid());
}

TEST(PidTest, fork) {
  pid_t mypid = getpid();
  EXPECT_EQ(mypid, folly::get_cached_pid());
  auto pid = fork();
  if (pid > 0) {
    // parent
    EXPECT_EQ(mypid, folly::get_cached_pid());
    EXPECT_NE(pid, mypid);
  } else if (pid == 0) {
    // child
    EXPECT_NE(mypid, folly::get_cached_pid()); // mypid still has parent's pid
    mypid = getpid(); // get child's pid
    EXPECT_EQ(mypid, folly::get_cached_pid());
    std::exit(0); // Do not print gtest results
  } else {
    PLOG(FATAL) << "Failed to fork()";
  }
}
