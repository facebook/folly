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

#include <folly/Portability.h>

#include <folly/CancellationToken.h>
#include <folly/experimental/coro/Baton.h>
#include <folly/experimental/coro/RustAdaptors.h>
#include <folly/experimental/coro/Sleep.h>
#include <folly/portability/GTest.h>
#include <folly/synchronization/Baton.h>

#include <chrono>

#if FOLLY_HAS_COROUTINES

template <typename T>
T getPollFuture(folly::coro::PollFuture<T> future) {
  while (true) {
    folly::Baton<> b;
    auto poll = future.poll([&] { b.post(); });
    if (poll) {
      return std::move(poll).value();
    }
    b.wait();
  }
}

template <typename T>
folly::Optional<T> getNextPollStream(folly::coro::PollStream<T>& stream) {
  while (true) {
    folly::Baton<> b;
    auto poll = stream.poll([&] { b.post(); });
    if (poll) {
      return std::move(poll).value();
    }
    b.wait();
  }
}

folly::coro::Task<int> task42() {
  co_await folly::coro::sleep(std::chrono::milliseconds{10});
  co_await folly::coro::sleep(std::chrono::milliseconds{10});
  co_return 42;
}

TEST(RustAdaptorsTest, PollFuture) {
  EXPECT_EQ(42, getPollFuture(folly::coro::PollFuture<int>(task42())));
}

TEST(RustAdaptorsTest, PollFutureSemiFuture) {
  EXPECT_EQ(42, getPollFuture(folly::coro::PollFuture<int>(task42().semi())));
}

folly::coro::AsyncGenerator<int> stream123() {
  co_await folly::coro::sleep(std::chrono::milliseconds{10});
  co_await folly::coro::sleep(std::chrono::milliseconds{10});
  co_yield 1;
  co_await folly::coro::sleep(std::chrono::milliseconds{10});
  co_yield 2;
  co_await folly::coro::sleep(std::chrono::milliseconds{10});
  co_await folly::coro::sleep(std::chrono::milliseconds{10});
  co_yield 3;
}

TEST(RustAdaptorsTest, PollStream) {
  auto stream = folly::coro::PollStream<int>(stream123());
  EXPECT_EQ(1, getNextPollStream(stream).value());
  EXPECT_EQ(2, getNextPollStream(stream).value());
  EXPECT_EQ(3, getNextPollStream(stream).value());
  EXPECT_FALSE(getNextPollStream(stream).hasValue());
}

folly::coro::Task<void> cancellationTask(bool& done) {
  folly::coro::Baton b;
  folly::CancellationCallback cb(
      co_await folly::coro::co_current_cancellation_token, [&] { b.post(); });
  co_await b;
  done = true;
}

TEST(RustAdaptorsTest, PollFutureCancellation) {
  bool done{false};
  {
    auto future = folly::coro::PollFuture<void>(cancellationTask(done));
    EXPECT_EQ(folly::none, future.poll([] {}));
    EXPECT_FALSE(done);
  }
  EXPECT_TRUE(done);
}

folly::coro::AsyncGenerator<int> cancellationStream(bool& done) {
  co_yield 1;
  co_yield 2;
  co_await cancellationTask(done);
}

TEST(RustAdaptorsTest, PollStreamCancellation) {
  bool done{false};
  {
    auto stream = folly::coro::PollStream<int>(cancellationStream(done));
    EXPECT_EQ(1, getNextPollStream(stream).value());
    EXPECT_EQ(2, getNextPollStream(stream).value());
    EXPECT_EQ(folly::none, stream.poll([] {}));
    EXPECT_FALSE(done);
  }
  EXPECT_TRUE(done);
}

#endif
