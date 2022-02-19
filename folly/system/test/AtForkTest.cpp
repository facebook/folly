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

#include <folly/system/AtFork.h>

#include <atomic>
#include <mutex>
#include <thread>

#include <glog/logging.h>

#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>

class AtForkTest : public testing::Test {};

TEST_F(AtForkTest, prepare) {
  int foo;
  bool forked = false;
  folly::AtFork::registerHandler(
      &foo,
      [&] {
        forked = true;
        return true;
      },
      [] {},
      [] {});
  auto pid = folly::kIsSanitizeThread //
      ? folly::AtFork::forkInstrumented(fork)
      : fork();
  PCHECK(pid != -1);
  if (pid) {
    int status;
    auto pid2 = waitpid(pid, &status, 0);
    EXPECT_EQ(status, 0);
    EXPECT_EQ(pid, pid2);
  } else {
    _exit(0);
  }
  EXPECT_TRUE(forked);
  forked = false;
  folly::AtFork::unregisterHandler(&foo);
  pid = fork();
  PCHECK(pid != -1);
  if (pid) {
    int status;
    auto pid2 = waitpid(pid, &status, 0);
    EXPECT_EQ(status, 0);
    EXPECT_EQ(pid, pid2);
  } else {
    _exit(0);
  }
  EXPECT_FALSE(forked);
}

TEST_F(AtForkTest, ordering) {
  std::atomic<bool> started{false};
  std::mutex a;
  std::mutex b;
  int foo;
  int foo2;
  folly::AtFork::registerHandler(
      &foo,
      [&] { return a.try_lock(); },
      [&] { a.unlock(); },
      [&] { a.unlock(); });
  folly::AtFork::registerHandler(
      &foo2,
      [&] { return b.try_lock(); },
      [&] { b.unlock(); },
      [&] { b.unlock(); });

  auto thr = std::thread([&]() {
    std::lock_guard<std::mutex> g(a);
    started = true;
    usleep(100);
    std::lock_guard<std::mutex> g2(b);
  });
  while (!started) {
  }
  auto pid = folly::kIsSanitizeThread //
      ? folly::AtFork::forkInstrumented(fork)
      : fork();
  PCHECK(pid != -1);
  if (pid) {
    int status;
    auto pid2 = waitpid(pid, &status, 0);
    EXPECT_TRUE(WIFEXITED(status));
    EXPECT_THAT(
        WEXITSTATUS(status),
        folly::kIsSanitizeThread //
            ? testing::AnyOfArray({0, 66})
            : testing::AnyOfArray({0}));
    EXPECT_EQ(pid, pid2);
  } else {
    _exit(0);
  }
  thr.join();
}
