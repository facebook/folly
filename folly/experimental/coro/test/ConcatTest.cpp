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

#include <folly/Portability.h>

#if FOLLY_HAS_COROUTINES

#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/Concat.h>
#include <folly/experimental/coro/Task.h>

#include <folly/portability/GTest.h>

using namespace folly::coro;

namespace {

AsyncGenerator<int> generateInts(int begin, int end) {
  for (int i = begin; i < end; i++) {
    co_await co_reschedule_on_current_executor;
    co_yield i;
  }
}

Task<std::vector<int>> toVector(AsyncGenerator<int> generator) {
  std::vector<int> result;
  while (auto x = co_await generator.next()) {
    result.push_back(*x);
  }
  co_return result;
}

} // namespace

class ConcatTest : public testing::Test {};

TEST_F(ConcatTest, ConcatSingle) {
  auto gen = concat(generateInts(0, 5));
  auto result = blockingWait(toVector(std::move(gen)));
  std::vector<int> expected{0, 1, 2, 3, 4};

  EXPECT_EQ(result, expected);
}

TEST_F(ConcatTest, ConcatMultiple) {
  auto gen =
      concat(generateInts(0, 5), generateInts(7, 10), generateInts(12, 15));
  auto result = blockingWait(toVector(std::move(gen)));
  std::vector<int> expected{0, 1, 2, 3, 4, 7, 8, 9, 12, 13, 14};

  EXPECT_EQ(result, expected);
}

#endif
