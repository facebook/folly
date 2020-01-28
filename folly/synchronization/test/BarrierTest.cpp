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

#include <folly/synchronization/test/Barrier.h>

#include <atomic>
#include <thread>
#include <vector>

#include <folly/portability/GTest.h>

using namespace folly::test;

class BarrierTest : public testing::Test {};

TEST_F(BarrierTest, basic_barrier_test) {
  constexpr unsigned kThreadCount = 10;

  std::vector<std::thread> threads{kThreadCount};
  Barrier gate{kThreadCount + 1};
  std::atomic<int> mismatchCount{0};
  std::atomic<bool> flag{false};

  for (auto& t : threads) {
    t = std::thread([&] {
      gate.wait();
      if (!flag.load()) {
        mismatchCount.fetch_add(1);
      }
    });
  }

  // Set flag here and make sure all the threads see it correctly after wait()
  // unblocks
  flag.store(true);
  gate.wait();

  for (auto& t : threads) {
    t.join();
  }

  EXPECT_EQ(mismatchCount.load(), 0);
}
