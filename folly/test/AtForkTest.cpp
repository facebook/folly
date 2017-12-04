/*
 * Copyright 2017-present Facebook, Inc.
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
#include <folly/detail/AtFork.h>

#include <folly/portability/GTest.h>
#include <glog/logging.h>

TEST(ThreadLocal, AtFork) {
  int foo;
  bool forked = false;
  folly::detail::AtFork::registerHandler(
      &foo, [&] { forked = true; }, [] {}, [] {});
  auto pid = fork();
  if (pid) {
    int status;
    auto pid2 = wait(&status);
    EXPECT_EQ(status, 0);
    EXPECT_EQ(pid, pid2);
  } else {
    exit(0);
  }
  EXPECT_TRUE(forked);
  forked = false;
  folly::detail::AtFork::unregisterHandler(&foo);
  pid = fork();
  if (pid) {
    int status;
    auto pid2 = wait(&status);
    EXPECT_EQ(status, 0);
    EXPECT_EQ(pid, pid2);
  } else {
    exit(0);
  }
  EXPECT_FALSE(forked);
}
