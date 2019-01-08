/*
 * Copyright 2019-present Facebook, Inc.
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

#include <folly/experimental/SingletonRelaxedCounter.h>

#include <cstddef>
#include <thread>

#include <boost/thread/barrier.hpp>

#include <folly/portability/GTest.h>

namespace folly {

namespace {
struct PrivateTag {};
} // namespace

using Counter = SingletonRelaxedCounter<size_t, PrivateTag>;

class SingletonRelaxedCounterTest : public testing::Test {};

TEST_F(SingletonRelaxedCounterTest, Basic) {
  EXPECT_EQ(0, Counter::count());
  Counter::add(3);
  EXPECT_EQ(3, Counter::count());
  Counter::sub(3);
  EXPECT_EQ(0, Counter::count());
}

TEST_F(SingletonRelaxedCounterTest, MultithreadCorrectness) {
  static constexpr size_t const kPerThreadIncrements = 1000000;
  static constexpr size_t const kNumThreads = 24;

  std::vector<std::thread> threads(kNumThreads);

  boost::barrier barrier{kNumThreads + 1};

  for (auto& thread : threads) {
    thread = std::thread([&] {
      barrier.wait(); // A
      barrier.wait(); // B
      for (size_t i = 0; i < kPerThreadIncrements; ++i) {
        Counter::add(1);
      }
      barrier.wait(); // C
      barrier.wait(); // D
      for (size_t i = 0; i < kPerThreadIncrements; ++i) {
        Counter::sub(1);
      }
      barrier.wait(); // E
      barrier.wait(); // F
    });
  }

  barrier.wait(); // A
  EXPECT_EQ(0, Counter::count());

  barrier.wait(); // B
  barrier.wait(); // C
  EXPECT_EQ(kPerThreadIncrements * kNumThreads, Counter::count());

  barrier.wait(); // D
  barrier.wait(); // E
  EXPECT_EQ(0, Counter::count());

  barrier.wait(); // F

  for (auto& thread : threads) {
    thread.join();
  }
}
} // namespace folly
