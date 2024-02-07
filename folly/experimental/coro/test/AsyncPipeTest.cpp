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

#include <folly/experimental/coro/AsyncGenerator.h>
#include <folly/experimental/coro/AsyncPipe.h>
#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/Task.h>

#include <folly/portability/GTest.h>

#include <string>

#if FOLLY_HAS_COROUTINES

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
      [generator_2 =
           std::move(generator)]() mutable -> folly::coro::Task<void> {
        for (int i = 0; i < 6; ++i) {
          auto val = co_await generator_2.next();
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

TEST(AsyncPipeTest, IsClosed) {
  auto pipe = folly::coro::AsyncPipe<int>::create();
  EXPECT_FALSE(pipe.second.isClosed());
  { auto gen = std::move(pipe.first); }
  EXPECT_TRUE(pipe.second.isClosed());
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

TEST(AsyncPipeTest, OnClosedCallbackCalledWhenGeneratorDestroyed) {
  auto onCloseCallbackBaton = folly::Baton<>();
  auto pipe = folly::coro::AsyncPipe<int>::create(
      [&]() { onCloseCallbackBaton.post(); } /* onClosed */);

  auto ex = folly::ManualExecutor();
  auto cancellationSource = folly::CancellationSource();
  auto fut = folly::coro::co_withCancellation(
                 cancellationSource.getToken(),
                 folly::coro::co_invoke(
                     [gen = std::move(pipe.first)]() mutable
                     -> folly::coro::Task<
                         folly::coro::AsyncGenerator<int&&>::NextResult> {
                       co_return co_await gen.next();
                     }))
                 .scheduleOn(&ex)
                 .start();
  ex.drain();
  EXPECT_FALSE(fut.isReady());
  EXPECT_FALSE(onCloseCallbackBaton.ready());

  cancellationSource.requestCancellation();

  ex.drain();
  EXPECT_TRUE(onCloseCallbackBaton.ready());
}

TEST(AsyncPipeTest, OnClosedCallbackCalledWhenPublisherClosesPipe) {
  auto onCloseCallbackBaton = folly::Baton<>();
  auto pipe = folly::coro::AsyncPipe<int>::create(
      [&]() { onCloseCallbackBaton.post(); } /* onClosed */);

  EXPECT_FALSE(onCloseCallbackBaton.ready());

  std::move(pipe.second).close();

  EXPECT_TRUE(onCloseCallbackBaton.ready());
}

TEST(AsyncPipeTest, OnClosedCallbackCalledWhenPublisherClosesPipeWithError) {
  auto onCloseCallbackExecuted = folly::Promise<folly::Unit>();
  auto pipe = folly::coro::AsyncPipe<int>::create(
      [&]() { onCloseCallbackExecuted.setValue(); } /* onClosed */);

  EXPECT_FALSE(onCloseCallbackExecuted.isFulfilled());

  std::move(pipe.second).close(std::runtime_error("error"));

  EXPECT_TRUE(onCloseCallbackExecuted.isFulfilled());
}

TEST(
    AsyncPipeTest,
    OnClosedCallbackJoinedWhenPublisherClosesPipeWhileGeneratorDestructing) {
  auto onCloseCallbackStartedBaton = folly::Baton<>();
  auto onCloseCallbackCompletionBaton = folly::Baton<>();
  auto pipe = folly::coro::AsyncPipe<int>::create([&]() {
    onCloseCallbackStartedBaton.post();
    onCloseCallbackCompletionBaton.wait();
  } /* onClosed */);

  auto ex = folly::ManualExecutor();
  auto cancellationSource = folly::CancellationSource();
  auto fut = folly::coro::co_withCancellation(
                 cancellationSource.getToken(),
                 folly::coro::co_invoke(
                     [gen = std::move(pipe.first)]() mutable
                     -> folly::coro::Task<
                         folly::coro::AsyncGenerator<int&&>::NextResult> {
                       co_return co_await gen.next();
                     }))
                 .scheduleOn(&ex)
                 .start();
  ex.drain();
  EXPECT_FALSE(fut.isReady());
  EXPECT_FALSE(onCloseCallbackStartedBaton.ready());

  auto cancelThread = std::thread([&]() {
    cancellationSource.requestCancellation();
    ex.drain();
  });
  onCloseCallbackStartedBaton.wait();

  auto pipeClosedBaton = folly::Baton<>();
  auto closePipeThread = std::thread([&]() {
    std::move(pipe.second).close();
    pipeClosedBaton.post();
  });
  /* sleep override */
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  EXPECT_FALSE(pipeClosedBaton.ready());

  onCloseCallbackCompletionBaton.post();
  pipeClosedBaton.wait();
  cancelThread.join();
  closePipeThread.join();
}

TEST(AsyncPipeTest, PublisherMustCloseIfCallbackSetAndGeneratorAlive) {
  EXPECT_DEATH(
      ([&]() {
        auto pipe = folly::coro::AsyncPipe<int>::create([]() {} /* onClosed */);
      })(),
      "If an onClosed callback is specified and the generator still exists, "
      "the publisher must explicitly close the pipe prior to destruction.");

  EXPECT_DEATH(
      ([&]() {
        auto pipe1 =
            folly::coro::AsyncPipe<int>::create([]() {} /* onClosed */);
        auto pipe2 = folly::coro::AsyncPipe<int>::create();
        pipe1.second = std::move(pipe2.second);
      })(),
      "If an onClosed callback is specified and the generator still exists, "
      "the publisher must explicitly close the pipe prior to destruction.");
}

TEST(BoundedAsyncPipeTest, PublishConsume) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    auto [generator, pipe] =
        folly::coro::BoundedAsyncPipe<int>::create(/* tokens */ 10);
    for (int i = 0; i < 15; ++i) {
      EXPECT_TRUE(co_await pipe.write(i));

      auto item = co_await generator.next();
      EXPECT_TRUE(item.has_value());
      EXPECT_EQ(item.value(), i);
    }
    std::move(pipe).close();
    EXPECT_FALSE(co_await generator.next());
  }());
}

TEST(BoundedAsyncPipeTest, PublisherBlocks) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    folly::ManualExecutor executor;
    auto [generator, pipe] =
        folly::coro::BoundedAsyncPipe<int>::create(/* tokens */ 10);

    for (size_t i = 0; i < 10; ++i) {
      co_await pipe.write(i);
    }

    // wrap in co_invoke() here, since write() accepts arguments by reference,
    // and temporaries may go out of scope
    auto writeFuture =
        folly::coro::co_invoke([&pipe_2 = pipe]() -> folly::coro::Task<bool> {
          co_return co_await pipe_2.write(20);
        })
            .scheduleOn(&executor)
            .start();
    executor.drain();
    EXPECT_FALSE(writeFuture.isReady());

    auto item = co_await generator.next();
    EXPECT_TRUE(item.has_value());

    executor.drain();
    EXPECT_TRUE(writeFuture.isReady());
  }());
}

TEST(BoundedAsyncPipeTest, IsClosed) {
  auto [generator, pipe] =
      folly::coro::BoundedAsyncPipe<int>::create(/* tokens */ 2);

  EXPECT_FALSE(pipe.isClosed());
  {
    // destroy the read end
    auto _ = std::move(generator);
  }
  EXPECT_TRUE(pipe.isClosed());
}

TEST(BoundedAsyncPipeTest, BlockingPublisherCanceledOnDestroy) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    folly::ManualExecutor executor;
    auto [generator, pipe] =
        folly::coro::BoundedAsyncPipe<int>::create(/* tokens */ 2);

    for (size_t i = 0; i < 2; ++i) {
      co_await pipe.write(i);
    }

    std::vector<folly::SemiFuture<bool>> futures;
    for (size_t i = 0; i < 5; ++i) {
      auto writeFuture =
          folly::coro::co_invoke([&pipe_2 = pipe]() -> folly::coro::Task<bool> {
            co_return co_await pipe_2.write(20);
          })
              .scheduleOn(&executor)
              .start();
      executor.drain();
      EXPECT_FALSE(writeFuture.isReady());
      futures.emplace_back(std::move(writeFuture));
    }

    {
      // destroy the read end
      auto _ = std::move(generator);
    }

    executor.drain();
    for (auto& future : futures) {
      EXPECT_TRUE(future.isReady());
      EXPECT_FALSE(std::move(future).get());
    }
  }());
}

TEST(BoundedAsyncPipeTest, PublisherFailsAfterDestroyWithRemainingTokens) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    auto [generator, pipe] =
        folly::coro::BoundedAsyncPipe<int>::create(/* tokens */ 2);
    {
      // destroy the read end
      auto _ = std::move(generator);
    }

    EXPECT_FALSE(co_await pipe.write(1));
    EXPECT_FALSE(co_await pipe.write(1));

    // No tokens left, blocking path also returns false
    EXPECT_FALSE(co_await pipe.write(1));
    EXPECT_FALSE(co_await pipe.write(1));
  }());
}

TEST(BoundedAsyncPipeTest, BlockingPublisherCancelsWithParent) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    folly::ManualExecutor executor;
    auto [generator, pipe] =
        folly::coro::BoundedAsyncPipe<int>::create(/* tokens */ 2);

    for (size_t i = 0; i < 2; ++i) {
      co_await pipe.write(i);
    }

    folly::CancellationSource cs;
    auto future = folly::coro::co_withCancellation(
                      cs.getToken(),
                      folly::coro::co_invoke(
                          [&pipe_2 = pipe]() -> folly::coro::Task<bool> {
                            co_return co_await pipe_2.write(100);
                          }))
                      .scheduleOn(&executor)
                      .start();
    executor.drain();
    EXPECT_FALSE(future.isReady());

    cs.requestCancellation();
    executor.drain();
    EXPECT_TRUE(future.isReady());
    EXPECT_TRUE(std::move(future).getTry().hasException());
  }());
}

TEST(BoundedAsyncPipeTest, ClosingPublisherEndsConsumer) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    auto [generator, pipe] =
        folly::coro::BoundedAsyncPipe<int>::create(/* tokens */ 2);

    for (size_t i = 0; i < 2; ++i) {
      co_await pipe.write(i);
    }
    std::move(pipe).close();

    EXPECT_TRUE(co_await generator.next());
    EXPECT_TRUE(co_await generator.next());
    EXPECT_FALSE(co_await generator.next());
  }());
}

TEST(BoundedAsyncPipeTest, ClosingPublisherWithException) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    auto [generator, pipe] =
        folly::coro::BoundedAsyncPipe<int>::create(/* tokens */ 2);

    for (size_t i = 0; i < 2; ++i) {
      co_await pipe.write(i);
    }
    std::move(pipe).close(std::runtime_error("error!"));

    EXPECT_TRUE(co_await generator.next());
    EXPECT_TRUE(co_await generator.next());

    auto itemTry = co_await folly::coro::co_awaitTry(generator.next());
    EXPECT_TRUE(itemTry.hasException());
  }());
}
#endif
