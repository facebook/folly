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
#include <folly/experimental/coro/AsyncPipe.h>
#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/Task.h>

#include <folly/portability/GTest.h>

#include <string>

TEST(AsyncPipeTest, PublishConsume) {
  auto pipe = folly::coro::AsyncPipe<int>::create();
  for (int i = 0; i < 5; ++i) {
    EXPECT_TRUE(pipe.second.write(i));
  }
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    for (int i = 0; i < 5; ++i) {
      auto val = co_await pipe.first.next();
      EXPECT_TRUE(val);
      EXPECT_EQ(*val, i);
    }
  }());
}

TEST(AsyncPipeTest, PublishLRValue) {
  auto pipe = folly::coro::AsyncPipe<std::string>::create();
  constexpr auto val = "a string";
  std::string val1 = val;
  EXPECT_TRUE(pipe.second.write(val1));
  EXPECT_TRUE(pipe.second.write(std::move(val1)));
  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    auto gen = std::move(pipe.first);
    for (int i = 0; i < 2; ++i) {
      auto val2 = co_await gen.next();
      EXPECT_TRUE(val2);
      EXPECT_EQ(*val2, val);
    }
  }());
}

TEST(AsyncPipeTest, PublishConsumeClose) {
  auto pipe = folly::coro::AsyncPipe<int>::create();
  for (int i = 0; i < 5; ++i) {
    EXPECT_TRUE(pipe.second.write(i));
  }
  std::move(pipe.second).close();
  EXPECT_FALSE(pipe.second.write(0));

  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    for (int i = 0; i < 5; ++i) {
      auto val = co_await pipe.first.next();
      EXPECT_TRUE(val);
      EXPECT_EQ(*val, i);
    }
    auto val = co_await pipe.first.next();
    EXPECT_FALSE(val);
  }());
}

TEST(AsyncPipeTest, PublishConsumeError) {
  auto pipe = folly::coro::AsyncPipe<int>::create();
  for (int i = 0; i < 5; ++i) {
    EXPECT_TRUE(pipe.second.write(i));
  }
  std::move(pipe.second).close(std::runtime_error(""));
  EXPECT_FALSE(pipe.second.write(0));

  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    for (int i = 0; i < 5; ++i) {
      auto val = co_await pipe.first.next();
      EXPECT_TRUE(val);
      EXPECT_EQ(*val, i);
    }
    EXPECT_THROW({ co_await pipe.first.next(); }, std::runtime_error);
  }());
}

TEST(AsyncPipeTest, PublishConsumeDestroy) {
  folly::coro::AsyncGenerator<int&&> gen;
  {
    auto pipe = folly::coro::AsyncPipe<int>::create();
    for (int i = 0; i < 5; ++i) {
      EXPECT_TRUE(pipe.second.write(i));
    }
    gen = std::move(pipe.first);
  }

  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    for (int i = 0; i < 5; ++i) {
      auto val = co_await gen.next();
      EXPECT_TRUE(val);
      EXPECT_EQ(*val, i);
    }
    auto val = co_await gen.next();
    EXPECT_FALSE(val);
  }());
}

TEST(AsyncPipeTest, PublishConsumeWithMoves) {
  auto [generator, pipe1] = folly::coro::AsyncPipe<int>::create();
  for (int i = 0; i < 2; ++i) {
    EXPECT_TRUE(pipe1.write(i));
  }
  // Move constructor
  auto pipe2 = std::move(pipe1);
  for (int i = 2; i < 4; ++i) {
    EXPECT_TRUE(pipe2.write(i));
  }
  // Move assignment (optional forces the assignment)
  std::optional<folly::coro::AsyncPipe<int>> pipe3;
  pipe3 = std::move(pipe2);
  for (int i = 4; i < 6; ++i) {
    EXPECT_TRUE(pipe3->write(i));
  }
  // Should still read all values
  folly::coro::blockingWait(
      [generator = std::move(generator)]() mutable -> folly::coro::Task<void> {
        for (int i = 0; i < 6; ++i) {
          auto val = co_await generator.next();
          EXPECT_TRUE(val);
          EXPECT_EQ(*val, i);
        }
      }());
}

TEST(AsyncPipeTest, BrokenPipe) {
  auto pipe = folly::coro::AsyncPipe<int>::create();
  EXPECT_TRUE(pipe.second.write(0));
  { auto gen = std::move(pipe.first); }
  EXPECT_FALSE(pipe.second.write(0));
  std::move(pipe.second).close();
}

TEST(AsyncPipeTest, WriteWhileBlocking) {
  auto pipe = folly::coro::AsyncPipe<int>::create();
  folly::ManualExecutor ex;

  auto fut = folly::coro::co_invoke(
                 [&]() -> folly::coro::Task<
                           folly::coro::AsyncGenerator<int&&>::NextResult> {
                   co_return co_await pipe.first.next();
                 })
                 .scheduleOn(&ex)
                 .start();
  ex.drain();
  EXPECT_FALSE(fut.isReady());

  EXPECT_TRUE(pipe.second.write(0));
  ex.drain();
  EXPECT_TRUE(fut.isReady());
  EXPECT_EQ(*std::move(fut).get(), 0);
}

TEST(AsyncPipeTest, CloseWhileBlocking) {
  auto pipe = folly::coro::AsyncPipe<int>::create();
  folly::ManualExecutor ex;

  auto fut = folly::coro::co_invoke(
                 [&]() -> folly::coro::Task<
                           folly::coro::AsyncGenerator<int&&>::NextResult> {
                   co_return co_await pipe.first.next();
                 })
                 .scheduleOn(&ex)
                 .start();
  ex.drain();
  EXPECT_FALSE(fut.isReady());

  std::move(pipe.second).close();
  ex.drain();
  EXPECT_TRUE(fut.isReady());
  EXPECT_FALSE(std::move(fut).get());
}

TEST(AsyncPipeTest, DestroyWhileBlocking) {
  auto pipe = folly::coro::AsyncPipe<int>::create();
  folly::ManualExecutor ex;

  auto fut = folly::coro::co_invoke(
                 [&]() -> folly::coro::Task<
                           folly::coro::AsyncGenerator<int&&>::NextResult> {
                   co_return co_await pipe.first.next();
                 })
                 .scheduleOn(&ex)
                 .start();
  ex.drain();
  EXPECT_FALSE(fut.isReady());

  { auto pipe_ = std::move(pipe.second); }
  ex.drain();
  EXPECT_TRUE(fut.isReady());
  EXPECT_FALSE(std::move(fut).get());
}
#endif
