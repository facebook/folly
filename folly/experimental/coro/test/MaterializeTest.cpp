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

#include <folly/experimental/coro/AsyncGenerator.h>
#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/CurrentExecutor.h>
#include <folly/experimental/coro/Materialize.h>
#include <folly/experimental/coro/Task.h>

#include <folly/portability/GTest.h>

using namespace folly::coro;

class MaterializeTest : public testing::Test {};

TEST_F(MaterializeTest, SimpleStream) {
  struct MyError : std::exception {};

  const int seenEndOfStream = 100;
  const int seenError = 200;

  std::vector<int> lastSeen(3, -1);
  std::vector<int> expectedSeen = {
      {seenEndOfStream, seenEndOfStream, seenError}};

  auto selectStream = [](int index) -> AsyncGenerator<int> {
    auto ints = [](int n) -> AsyncGenerator<int> {
      for (int i = 0; i <= n; ++i) {
        co_await co_reschedule_on_current_executor;
        co_yield i;
      }
    };

    auto failing = []() -> AsyncGenerator<int> {
      co_yield 0;
      co_yield 1;
      throw MyError{};
    };

    if (index == 0) {
      return ints(4);
    } else if (index == 1) {
      return ints(3);
    }
    return failing();
  };

  auto test = [&](int index) -> Task<void> {
    auto generator = materialize(selectStream(index));
    while (auto item = co_await generator.next()) {
      auto event = std::move(*item);
      CHECK(index >= 0 && index <= 2);

      if (event.hasValue()) {
        auto value = std::move(event).value();
        CHECK_EQ(lastSeen[index] + 1, value);
        lastSeen[index] = value;
      } else if (event.hasNone()) {
        // None
        if (index == 0) {
          CHECK_EQ(4, lastSeen[index]);
        } else if (index == 1) {
          CHECK_EQ(3, lastSeen[index]);
        } else if (event.hasError()) {
          // Stream 2 should have completed with an error not EndOfStream.
          CHECK(false);
        }
        lastSeen[index] = seenEndOfStream;
      } else {
        // Error
        CHECK_EQ(2, index);
        CHECK_EQ(1, lastSeen[index]);
        CHECK(std::move(event).error().is_compatible_with<MyError>());
        lastSeen[index] = seenError;
      }
    }

    CHECK_EQ(expectedSeen[index], lastSeen[index]);
  };
  blockingWait(test(0));
  blockingWait(test(1));
  blockingWait(test(2));
}

#endif
