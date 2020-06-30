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
#include <folly/experimental/coro/Filter.h>

#include <folly/portability/GTest.h>

using namespace folly::coro;

class FilterTest : public testing::Test {};

TEST_F(FilterTest, SimpleStream) {
  const auto allNumbers = []() -> AsyncGenerator<int> {
    for (int i = 0; i < 10; ++i) {
      co_yield i;
    }
  };

  auto evenNumbers = filter(allNumbers(), [](int i) { return i % 2 == 0; });
  EXPECT_EQ(0, blockingWait(evenNumbers.next()).value());
  EXPECT_EQ(2, blockingWait(evenNumbers.next()).value());
  EXPECT_EQ(4, blockingWait(evenNumbers.next()).value());
  EXPECT_EQ(6, blockingWait(evenNumbers.next()).value());
  EXPECT_EQ(8, blockingWait(evenNumbers.next()).value());
  EXPECT_FALSE(blockingWait(evenNumbers.next()));
}

TEST_F(FilterTest, EmptyInputStream) {
  const auto emptyStream = []() -> AsyncGenerator<int> { co_return; };

  auto emptyStreamFiltered = filter(emptyStream(), [](int) { return true; });
  EXPECT_FALSE(blockingWait(emptyStreamFiltered.next()));
}

TEST_F(FilterTest, EmptyOutputStream) {
  const auto nonEmptyStream = []() -> AsyncGenerator<int> {
    co_yield 0;
    co_yield 1;
    co_yield 2;
  };

  auto emptyOutputStream = filter(nonEmptyStream(), [](int) { return false; });
  EXPECT_FALSE(blockingWait(emptyOutputStream.next()));
}

TEST_F(FilterTest, ThrowingStream) {
  struct Exception : std::exception {};

  const auto throwingStream = []() -> AsyncGenerator<int> {
    co_yield 0;
    co_yield 1;
    throw Exception{};
  };

  auto throwingStreamFiltered =
      filter(throwingStream(), [](int) { return true; });
  EXPECT_EQ(0, blockingWait(throwingStreamFiltered.next()).value());
  EXPECT_EQ(1, blockingWait(throwingStreamFiltered.next()).value());
  EXPECT_THROW(blockingWait(throwingStreamFiltered.next()), Exception);
}

TEST_F(FilterTest, ThrowingPredicate) {
  struct Exception : std::exception {};

  auto nonThrowingStream = []() -> AsyncGenerator<int> {
    co_yield 0;
    co_yield 1;
    co_yield 2;
  };

  auto evenNumbers = filter(nonThrowingStream(), [](int i) {
    if (i == 1) {
      throw Exception{};
    };
    return true;
  });
  EXPECT_EQ(0, blockingWait(evenNumbers.next()).value());
  EXPECT_THROW(blockingWait(evenNumbers.next()), Exception);
}

#endif
