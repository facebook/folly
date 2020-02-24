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

#include <folly/init/Phase.h>

#include <folly/Singleton.h>
#include <folly/portability/GTest.h>

#include <glog/logging.h>

#include <thread>

/// Types

struct Global {
  Global() {
    CHECK(folly::get_process_phase() == folly::ProcessPhase::Init);
  }

  ~Global() {
    CHECK(folly::get_process_phase() >= folly::ProcessPhase::Exit);
  }
};

/// Variables

static Global global;

/// Tests

TEST(InitTest, basic) {
  ASSERT_EQ(folly::get_process_phase(), folly::ProcessPhase::Regular);
}

TEST(InitTest, fork) {
  std::thread t1([] {
    ASSERT_EQ(folly::get_process_phase(), folly::ProcessPhase::Regular);
  });
  t1.join();
  folly::SingletonVault::singleton()->destroyInstances();
  auto pid = fork();
  folly::SingletonVault::singleton()->reenableInstances();
  if (pid > 0) {
    // parent
    int status = -1;
    auto pid2 = waitpid(pid, &status, 0);
    EXPECT_EQ(status, 0);
    EXPECT_EQ(pid, pid2);
    ASSERT_EQ(folly::get_process_phase(), folly::ProcessPhase::Regular);
  } else if (pid == 0) {
    // child
    std::thread t2([] {
      ASSERT_EQ(folly::get_process_phase(), folly::ProcessPhase::Regular);
    });
    t2.join();
    std::exit(0); // Do not print gtest results
  } else {
    PLOG(FATAL) << "Failed to fork()";
  }
}
