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

#include <folly/CancellationToken.h>
#include <folly/experimental/coro/AsyncGenerator.h>
#include <folly/experimental/coro/Baton.h>
#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/Collect.h>
#include <folly/experimental/coro/CurrentExecutor.h>
#include <folly/experimental/coro/Task.h>
#include <folly/experimental/coro/Transform.h>

#include <folly/portability/GTest.h>

using namespace folly::coro;

class TransformTest : public testing::Test {};

TEST_F(TransformTest, SimpleStream) {
  struct MyError : std::exception {};

  const float seenEndOfStream = 100.0f;
  const float seenError = 200.0f;

  std::vector<float> lastSeen(3, -1);
  std::vector<float> expectedSeen = {
      {seenEndOfStream, seenEndOfStream, seenError}};

  int totalEventCount = 0;

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
    auto generator =
        transform(selectStream(index), [](int i) { return i * 1.0f; });
    try {
      while (auto item = co_await generator.next()) {
        ++totalEventCount;

        auto value = *item;
        CHECK(index >= 0 && index <= 2);
        CHECK_EQ(lastSeen[index] + 1.0f, value);
        lastSeen[index] = value;
      }

      // EndOfStream
      if (index == 0) {
        CHECK_EQ(4, lastSeen[index]);
      } else if (index == 1) {
        CHECK_EQ(3, lastSeen[index]);
      } else {
        // Stream 2 should have completed with an error not EndOfStream.
        CHECK(false);
      }
      lastSeen[index] = seenEndOfStream;

    } catch (const MyError&) {
      // Error
      CHECK_EQ(2, index);
      CHECK_EQ(1, lastSeen[index]);
      lastSeen[index] = seenError;
    }

    CHECK_EQ(expectedSeen[index], lastSeen[index]);
  };
  blockingWait(test(0));
  blockingWait(test(1));
  blockingWait(test(2));
}

template <typename Ref, typename Value = folly::remove_cvref_t<Ref>>
folly::coro::AsyncGenerator<Ref, Value> neverStream() {
  folly::coro::Baton baton;
  folly::CancellationCallback cb{
      co_await folly::coro::co_current_cancellation_token,
      [&] { baton.post(); }};
  co_await baton;
}

TEST_F(TransformTest, CancellationTokenPropagatesFromConsumer) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    folly::CancellationSource cancelSource;
    bool suspended = false;
    bool done = false;
    co_await folly::coro::collectAll(
        folly::coro::co_withCancellation(
            cancelSource.getToken(),
            [&]() -> folly::coro::Task<void> {
              auto stream =
                  transform(neverStream<float>(), [](float) { return 42; });
              suspended = true;
              auto result = co_await stream.next();
              CHECK(!result.has_value());
              done = true;
            }()),
        [&]() -> folly::coro::Task<void> {
          co_await folly::coro::co_reschedule_on_current_executor;
          co_await folly::coro::co_reschedule_on_current_executor;
          co_await folly::coro::co_reschedule_on_current_executor;
          CHECK(suspended);
          CHECK(!done);
          cancelSource.requestCancellation();
        }());
    CHECK(done);
  }());
}

#endif
