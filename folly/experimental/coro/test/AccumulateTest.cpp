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

#include <folly/experimental/coro/Accumulate.h>
#include <folly/experimental/coro/BlockingWait.h>

#include <folly/portability/GTest.h>

using namespace folly::coro;

namespace {

AsyncGenerator<int> generateInts(int begin, int end) {
  for (int i = begin; i < end; i++) {
    co_await co_reschedule_on_current_executor;
    co_yield i;
  }
}

} // namespace

class AccumulateTest : public testing::Test {};

TEST_F(AccumulateTest, NoOperationProvided) {
  auto result = blockingWait(accumulate(generateInts(0, 5), 0));
  auto expected = 0 + 1 + 2 + 3 + 4;

  EXPECT_EQ(result, expected);
}

TEST_F(AccumulateTest, OperationProvided) {
  auto result =
      blockingWait(accumulate(generateInts(1, 5), 1, std::multiplies{}));
  auto expected = 1 * 2 * 3 * 4;

  EXPECT_EQ(result, expected);
}

#endif
