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

#include <folly/ScopeGuard.h>
#include <folly/executors/ManualExecutor.h>
#include <folly/executors/SerialExecutor.h>
#include <folly/experimental/channels/ConsumeChannel.h>
#include <folly/experimental/channels/Transform.h>
#include <folly/experimental/channels/test/ChannelTestUtil.h>
#include <folly/experimental/coro/AsyncGenerator.h>
#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>

namespace folly {
namespace channels {

using namespace testing;
using namespace std::string_literals;

class TransformFixture : public Test {
 protected:
  TransformFixture() {}

  ~TransformFixture() { executor_.drain(); }

  ChannelCallbackHandle processValues(Receiver<std::string> receiver) {
    return consumeChannelWithCallback(
        std::move(receiver),
        &executor_,
        [=](folly::Try<std::string> resultTry) -> folly::coro::Task<bool> {
          onNext_(std::move(resultTry));
          co_return true;
        });
  }

  folly::ManualExecutor executor_;
  StrictMock<MockNextCallback<std::string>> onNext_;
};

class SimpleTransformFixture : public TransformFixture {};

TEST_F(SimpleTransformFixture, ReceiveValue_ReturnTransformedValue) {
  auto [untransformedReceiver, sender] = Channel<int>::create();
  auto transformedReceiver = transform(
      std::move(untransformedReceiver),
      &executor_,
      [](folly::Try<int> result) -> folly::coro::AsyncGenerator<std::string&&> {
        if (result.value() % 2 == 0) {
          co_yield folly::to<std::string>(result.value());
        } else {
          co_return;
        }
      });

  EXPECT_CALL(onNext_, onValue("2"));
  EXPECT_CALL(onNext_, onValue("4"));
  EXPECT_CALL(onNext_, onClosed());

  sender.write(1);
  sender.write(2);
  executor_.drain();

  auto callbackHandle = processValues(std::move(transformedReceiver));

  sender.write(3);
  sender.write(4);
  executor_.drain();

  std::move(sender).close();
  executor_.drain();
}

TEST_F(SimpleTransformFixture, ReceiveValue_Close) {
  bool close = false;
  auto [untransformedReceiver, sender] = Channel<int>::create();
  auto transformedReceiver = transform(
      std::move(untransformedReceiver),
      &executor_,
      [&](folly::Try<int> result)
          -> folly::coro::AsyncGenerator<std::string&&> {
        if (close) {
          throw OnClosedException();
        }
        co_yield folly::to<std::string>(result.value());
      });

  EXPECT_CALL(onNext_, onValue("1"));
  EXPECT_CALL(onNext_, onValue("2"));
  EXPECT_CALL(onNext_, onClosed());

  auto callbackHandle = processValues(std::move(transformedReceiver));

  sender.write(1);
  sender.write(2);
  executor_.drain();

  close = true;
  sender.write(3);
  sender.write(4);
  executor_.drain();
}

TEST_F(SimpleTransformFixture, ReceiveValue_Throw_InCoroutine) {
  bool throwException = false;
  auto [untransformedReceiver, sender] = Channel<int>::create();
  auto transformedReceiver = transform(
      std::move(untransformedReceiver),
      &executor_,
      [&](folly::Try<int> result)
          -> folly::coro::AsyncGenerator<std::string&&> {
        if (throwException) {
          throw std::runtime_error("Error");
        }
        co_yield folly::to<std::string>(result.value());
      });

  EXPECT_CALL(onNext_, onValue("1"));
  EXPECT_CALL(onNext_, onValue("2"));
  EXPECT_CALL(onNext_, onRuntimeError("std::runtime_error: Error"));

  auto callbackHandle = processValues(std::move(transformedReceiver));

  sender.write(1);
  sender.write(2);
  executor_.drain();

  throwException = true;
  sender.write(3);
  sender.write(4);
  executor_.drain();
}

TEST_F(SimpleTransformFixture, ReceiveValue_Throw_InNonCoroutine) {
  bool throwException = false;
  auto [untransformedReceiver, sender] = Channel<int>::create();
  auto transformedReceiver = transform(
      std::move(untransformedReceiver),
      &executor_,
      [&](folly::Try<int> result)
          -> folly::coro::AsyncGenerator<std::string&&> {
        if (throwException) {
          throw std::runtime_error("Error");
        }
        return folly::coro::co_invoke(
            [=]() -> folly::coro::AsyncGenerator<std::string&&> {
              co_yield folly::to<std::string>(result.value());
            });
      });

  EXPECT_CALL(onNext_, onValue("1"));
  EXPECT_CALL(onNext_, onValue("2"));
  EXPECT_CALL(onNext_, onRuntimeError("std::runtime_error: Error"));

  auto callbackHandle = processValues(std::move(transformedReceiver));

  sender.write(1);
  sender.write(2);
  executor_.drain();

  throwException = true;
  sender.write(3);
  sender.write(4);
  executor_.drain();
}

TEST_F(SimpleTransformFixture, ReceiveClosed_Close) {
  auto [untransformedReceiver, sender] = Channel<int>::create();
  auto transformedReceiver = transform(
      std::move(untransformedReceiver),
      &executor_,
      [&](folly::Try<int> result)
          -> folly::coro::AsyncGenerator<std::string&&> {
        if (result.hasException()) {
          EXPECT_TRUE(result.hasException<OnClosedException>());
        }
        co_yield folly::to<std::string>(result.value());
      });

  EXPECT_CALL(onNext_, onValue("1"));
  EXPECT_CALL(onNext_, onValue("2"));
  EXPECT_CALL(onNext_, onClosed());

  auto callbackHandle = processValues(std::move(transformedReceiver));

  sender.write(1);
  sender.write(2);
  executor_.drain();

  std::move(sender).close();
  executor_.drain();
}

TEST_F(SimpleTransformFixture, ReceiveClosed_Throw) {
  auto [untransformedReceiver, sender] = Channel<int>::create();
  auto transformedReceiver = transform(
      std::move(untransformedReceiver),
      &executor_,
      [&](folly::Try<int> result)
          -> folly::coro::AsyncGenerator<std::string&&> {
        if (result.hasException()) {
          EXPECT_TRUE(result.hasException<OnClosedException>());
          throw std::runtime_error("Error");
        }
        co_yield folly::to<std::string>(result.value());
      });

  EXPECT_CALL(onNext_, onValue("1"));
  EXPECT_CALL(onNext_, onValue("2"));
  EXPECT_CALL(onNext_, onRuntimeError("std::runtime_error: Error"));

  auto callbackHandle = processValues(std::move(transformedReceiver));

  sender.write(1);
  sender.write(2);
  executor_.drain();

  std::move(sender).close();
  executor_.drain();
}

TEST_F(SimpleTransformFixture, ReceiveException_Close) {
  auto [untransformedReceiver, sender] = Channel<int>::create();
  auto transformedReceiver = transform(
      std::move(untransformedReceiver),
      &executor_,
      [&](folly::Try<int> result)
          -> folly::coro::AsyncGenerator<std::string&&> {
        if (result.hasException()) {
          EXPECT_THROW(result.throwUnlessValue(), std::runtime_error);
          // We will swallow the exception and move on.
          throw OnClosedException();
        }
        co_yield folly::to<std::string>(result.value());
      });

  EXPECT_CALL(onNext_, onValue("1"));
  EXPECT_CALL(onNext_, onValue("2"));
  EXPECT_CALL(onNext_, onClosed());

  auto callbackHandle = processValues(std::move(transformedReceiver));

  sender.write(1);
  sender.write(2);
  executor_.drain();

  std::move(sender).close(std::runtime_error("Error"));
  executor_.drain();
}

TEST_F(SimpleTransformFixture, ReceiveException_Throw) {
  auto [untransformedReceiver, sender] = Channel<int>::create();
  auto transformedReceiver = transform(
      std::move(untransformedReceiver),
      &executor_,
      [&](folly::Try<int> result)
          -> folly::coro::AsyncGenerator<std::string&&> {
        if (result.hasException()) {
          EXPECT_THROW(result.throwUnlessValue(), std::runtime_error);
        }
        co_yield folly::to<std::string>(result.value());
      });

  EXPECT_CALL(onNext_, onValue("1"));
  EXPECT_CALL(onNext_, onValue("2"));
  EXPECT_CALL(onNext_, onRuntimeError("std::runtime_error: Error"));

  auto callbackHandle = processValues(std::move(transformedReceiver));

  sender.write(1);
  sender.write(2);
  executor_.drain();

  std::move(sender).close(std::runtime_error("Error"));
  executor_.drain();
}

TEST_F(SimpleTransformFixture, Cancelled) {
  auto [untransformedReceiver, sender] = Channel<int>::create();
  auto transformedReceiver = transform(
      std::move(untransformedReceiver),
      &executor_,
      [&](folly::Try<int> result)
          -> folly::coro::AsyncGenerator<std::string&&> {
        co_yield folly::to<std::string>(result.value());
      });

  EXPECT_CALL(onNext_, onValue("1"));
  EXPECT_CALL(onNext_, onValue("2"));
  EXPECT_CALL(onNext_, onCancelled());

  auto callbackHandle = processValues(std::move(transformedReceiver));

  sender.write(1);
  sender.write(2);
  executor_.drain();

  LOG(INFO) << "Cancelling...";
  callbackHandle.reset();
  executor_.drain();
  LOG(INFO) << "Finished cancelling";
}

TEST_F(SimpleTransformFixture, Chained) {
  auto [receiver, sender] = Channel<int>::create();
  for (int i = 0; i < 10; i++) {
    receiver = transform(
        std::move(receiver),
        &executor_,
        [](folly::Try<int> result) -> folly::coro::AsyncGenerator<int> {
          co_yield result.value() + 1;
        });
  }
  auto callbackHandle = processValues(transform(
      std::move(receiver),
      &executor_,
      [](folly::Try<int> result) -> folly::coro::AsyncGenerator<std::string&&> {
        co_yield folly::to<std::string>(result.value());
      }));
  executor_.drain();

  EXPECT_CALL(onNext_, onValue("11"));
  EXPECT_CALL(onNext_, onValue("12"));
  EXPECT_CALL(onNext_, onValue("13"));
  EXPECT_CALL(onNext_, onValue("14"));
  EXPECT_CALL(onNext_, onClosed());

  sender.write(1);
  sender.write(2);
  sender.write(3);
  sender.write(4);
  std::move(sender).close();
  executor_.drain();
}

class TransformFixtureStress : public Test {
 protected:
  TransformFixtureStress()
      : producer_(std::make_unique<StressTestProducer<int>>(
            [value = 0]() mutable { return value++; })),
        consumer_(std::make_unique<StressTestConsumer<std::string>>(
            ConsumptionMode::CallbackWithHandle,
            [lastReceived = -1](std::string value) mutable {
              EXPECT_EQ(folly::to<int>(value), ++lastReceived);
            })) {}

  static constexpr std::chrono::milliseconds kTestTimeout =
      std::chrono::milliseconds{5000};

  std::unique_ptr<StressTestProducer<int>> producer_;
  std::unique_ptr<StressTestConsumer<std::string>> consumer_;
};

TEST_F(TransformFixtureStress, Close) {
  auto [receiver, sender] = Channel<int>::create();
  producer_->startProducing(std::move(sender), std::nullopt /* closeEx */);

  folly::CPUThreadPoolExecutor transformExecutor(1);
  consumer_->startConsuming(transform(
      std::move(receiver),
      folly::SerialExecutor::create(&transformExecutor),
      [](folly::Try<int> result) -> folly::coro::AsyncGenerator<std::string> {
        co_yield folly::to<std::string>(std::move(result.value()));
      }));

  /* sleep override */
  std::this_thread::sleep_for(kTestTimeout);
  producer_->stopProducing();
  EXPECT_EQ(consumer_->waitForClose().get(), CloseType::NoException);
}

TEST_F(TransformFixtureStress, Cancel) {
  auto [receiver, sender] = Channel<int>::create();
  producer_->startProducing(std::move(sender), std::nullopt /* closeEx */);

  folly::CPUThreadPoolExecutor transformExecutor(1);
  consumer_->startConsuming(transform(
      std::move(receiver),
      folly::SerialExecutor::create(&transformExecutor),
      [](folly::Try<int> result) -> folly::coro::AsyncGenerator<std::string> {
        co_yield folly::to<std::string>(std::move(result.value()));
      }));

  /* sleep override */
  std::this_thread::sleep_for(kTestTimeout);
  consumer_->cancel();
  EXPECT_EQ(consumer_->waitForClose().get(), CloseType::Cancelled);
}

TEST_F(TransformFixtureStress, Close_ThenCancelImmediately) {
  auto [receiver, sender] = Channel<int>::create();
  producer_->startProducing(std::move(sender), std::nullopt /* closeEx */);

  folly::CPUThreadPoolExecutor transformExecutor(1);
  consumer_->startConsuming(transform(
      std::move(receiver),
      folly::SerialExecutor::create(&transformExecutor),
      [](folly::Try<int> result) -> folly::coro::AsyncGenerator<std::string> {
        co_yield folly::to<std::string>(std::move(result.value()));
      }));

  /* sleep override */
  std::this_thread::sleep_for(kTestTimeout);
  producer_->stopProducing();
  consumer_->cancel();

  EXPECT_THAT(
      consumer_->waitForClose().get(),
      AnyOf(Eq(CloseType::NoException), Eq(CloseType::Cancelled)));
}

TEST_F(TransformFixtureStress, Cancel_ThenCloseImmediately) {
  auto [receiver, sender] = Channel<int>::create();
  producer_->startProducing(std::move(sender), std::nullopt /* closeEx */);

  folly::CPUThreadPoolExecutor transformExecutor(1);
  consumer_->startConsuming(transform(
      std::move(receiver),
      folly::SerialExecutor::create(&transformExecutor),
      [](folly::Try<int> result) -> folly::coro::AsyncGenerator<std::string> {
        co_yield folly::to<std::string>(std::move(result.value()));
      }));

  /* sleep override */
  std::this_thread::sleep_for(kTestTimeout);
  consumer_->cancel();
  producer_->stopProducing();

  EXPECT_THAT(
      consumer_->waitForClose().get(),
      AnyOf(Eq(CloseType::NoException), Eq(CloseType::Cancelled)));
}

class ResumableTransformFixture : public TransformFixture {};

TEST_F(
    ResumableTransformFixture,
    InitializesAndReturnsTransformedValues_ThenClosed) {
  auto [untransformedReceiver, sender] = Channel<int>::create();
  auto transformedReceiver = resumableTransform(
      &executor_,
      [alreadyInitialized = false,
       receiver = std::move(untransformedReceiver)]() mutable
      -> folly::coro::Task<std::pair<std::vector<std::string>, Receiver<int>>> {
        if (alreadyInitialized) {
          throw OnClosedException();
        }
        alreadyInitialized = true;
        co_return std::make_pair(toVector("abc"s, "def"s), std::move(receiver));
      },
      [](folly::Try<int> result) -> folly::coro::AsyncGenerator<std::string&&> {
        co_yield folly::to<std::string>(result.value());
      });

  EXPECT_CALL(onNext_, onValue("abc"));
  EXPECT_CALL(onNext_, onValue("def"));
  EXPECT_CALL(onNext_, onValue("1"));
  EXPECT_CALL(onNext_, onValue("2"));
  EXPECT_CALL(onNext_, onValue("3"));
  EXPECT_CALL(onNext_, onValue("4"));
  EXPECT_CALL(onNext_, onClosed());

  sender.write(1);
  sender.write(2);
  executor_.drain();

  auto callbackHandle = processValues(std::move(transformedReceiver));

  sender.write(3);
  sender.write(4);
  executor_.drain();

  std::move(sender).close();
  executor_.drain();
}

TEST_F(
    ResumableTransformFixture,
    InitializesAndReturnsTransformedValues_ThenCancelled) {
  auto [untransformedReceiver, sender] = Channel<int>::create();
  auto transformedReceiver = resumableTransform(
      &executor_,
      [alreadyInitialized = false,
       receiver = std::move(untransformedReceiver)]() mutable
      -> folly::coro::Task<std::pair<std::vector<std::string>, Receiver<int>>> {
        if (alreadyInitialized) {
          throw OnClosedException();
        }
        alreadyInitialized = true;
        co_return std::make_pair(toVector("abc"s, "def"s), std::move(receiver));
      },
      [](folly::Try<int> result) -> folly::coro::AsyncGenerator<std::string&&> {
        co_yield folly::to<std::string>(result.value());
      });

  EXPECT_CALL(onNext_, onValue("abc"));
  EXPECT_CALL(onNext_, onValue("def"));
  EXPECT_CALL(onNext_, onValue("1"));
  EXPECT_CALL(onNext_, onValue("2"));
  EXPECT_CALL(onNext_, onValue("3"));
  EXPECT_CALL(onNext_, onValue("4"));
  EXPECT_CALL(onNext_, onCancelled());

  sender.write(1);
  sender.write(2);
  executor_.drain();

  auto callbackHandle = processValues(std::move(transformedReceiver));

  sender.write(3);
  sender.write(4);
  executor_.drain();

  callbackHandle.reset();
  executor_.drain();
}

TEST_F(
    ResumableTransformFixture,
    InitializesAndReturnsTransformedValues_ThenClosed_CancelledBeforeReinit) {
  auto [untransformedReceiver, sender] = Channel<int>::create();
  auto transformedReceiver = resumableTransform(
      &executor_,
      [alreadyInitialized = false,
       receiver = std::move(untransformedReceiver)]() mutable
      -> folly::coro::Task<std::pair<std::vector<std::string>, Receiver<int>>> {
        if (alreadyInitialized) {
          throw OnClosedException();
        }
        alreadyInitialized = true;
        co_return std::make_pair(toVector("abc"s, "def"s), std::move(receiver));
      },
      [](folly::Try<int> result) -> folly::coro::AsyncGenerator<std::string&&> {
        LOG(INFO) << "Got value " << result.hasException();
        co_yield folly::to<std::string>(result.value());
      });

  EXPECT_CALL(onNext_, onValue("abc"));
  EXPECT_CALL(onNext_, onValue("def"));
  EXPECT_CALL(onNext_, onValue("1"));
  EXPECT_CALL(onNext_, onValue("2"));
  EXPECT_CALL(onNext_, onCancelled());

  auto callbackHandle = processValues(std::move(transformedReceiver));

  sender.write(1);
  sender.write(2);
  executor_.drain();

  std::move(sender).close();
  callbackHandle.reset();
  executor_.drain();
}

TEST_F(
    ResumableTransformFixture,
    FirstReceiverCloses_ReinitializesWithNewReceiver_ThenClosed) {
  auto [untransformedReceiver, sender] = Channel<int>::create();
  auto transformedReceiver = resumableTransform(
      &executor_,
      [numTimesInitialized = 0, &receiver = untransformedReceiver]() mutable
      -> folly::coro::Task<std::pair<std::vector<std::string>, Receiver<int>>> {
        if (numTimesInitialized > 1) {
          throw OnClosedException();
        }
        numTimesInitialized++;
        co_return std::make_pair(
            toVector(folly::to<std::string>("abc", numTimesInitialized)),
            std::move(receiver));
      },
      [](folly::Try<int> result) -> folly::coro::AsyncGenerator<std::string&&> {
        co_yield folly::to<std::string>(result.value());
      });

  EXPECT_CALL(onNext_, onValue("abc1"));
  EXPECT_CALL(onNext_, onValue("1"));
  EXPECT_CALL(onNext_, onValue("2"));
  EXPECT_CALL(onNext_, onValue("abc2"));
  EXPECT_CALL(onNext_, onValue("3"));
  EXPECT_CALL(onNext_, onValue("4"));
  EXPECT_CALL(onNext_, onClosed());

  auto callbackHandle = processValues(std::move(transformedReceiver));

  sender.write(1);
  sender.write(2);
  executor_.drain();

  std::move(sender).close();
  std::tie(untransformedReceiver, sender) = Channel<int>::create();
  executor_.drain();

  sender.write(3);
  sender.write(4);
  executor_.drain();

  std::move(sender).close();
  executor_.drain();
}

TEST_F(
    ResumableTransformFixture,
    FirstReceiverClosesWithException_NoReinitialization_Rethrows) {
  auto [untransformedReceiver, sender] = Channel<int>::create();
  auto transformedReceiver = resumableTransform(
      &executor_,
      [alreadyInitialized = false,
       receiver = std::move(untransformedReceiver)]() mutable
      -> folly::coro::Task<std::pair<std::vector<std::string>, Receiver<int>>> {
        CHECK(!alreadyInitialized);
        alreadyInitialized = true;
        co_return std::make_pair(toVector("abc"s), std::move(receiver));
      },
      [](folly::Try<int> result) -> folly::coro::AsyncGenerator<std::string&&> {
        co_yield folly::to<std::string>(result.value());
      });

  EXPECT_CALL(onNext_, onValue("abc"));
  EXPECT_CALL(onNext_, onValue("1"));
  EXPECT_CALL(onNext_, onValue("2"));
  EXPECT_CALL(onNext_, onRuntimeError("std::runtime_error: Error"));

  auto callbackHandle = processValues(std::move(transformedReceiver));

  sender.write(1);
  sender.write(2);
  executor_.drain();

  std::move(sender).close(std::runtime_error("Error"));
  executor_.drain();
}

TEST_F(
    ResumableTransformFixture,
    FirstReceiverClosesWithException_TransformSwallows_Reinitialization) {
  auto [untransformedReceiver, sender] = Channel<int>::create();
  auto transformedReceiver = resumableTransform(
      &executor_,
      [numTimesInitialized = 0, &receiver = untransformedReceiver]() mutable
      -> folly::coro::Task<std::pair<std::vector<std::string>, Receiver<int>>> {
        numTimesInitialized++;
        co_return std::make_pair(
            toVector(folly::to<std::string>("abc", numTimesInitialized)),
            std::move(receiver));
      },
      [](folly::Try<int> result) -> folly::coro::AsyncGenerator<std::string&&> {
        if (result.hasValue()) {
          co_yield folly::to<std::string>(result.value());
        } else {
          EXPECT_THROW(result.throwUnlessValue(), std::runtime_error);
        }
      });

  EXPECT_CALL(onNext_, onValue("abc1"));
  EXPECT_CALL(onNext_, onValue("1"));
  EXPECT_CALL(onNext_, onValue("2"));
  EXPECT_CALL(onNext_, onValue("abc2"));
  EXPECT_CALL(onNext_, onValue("3"));
  EXPECT_CALL(onNext_, onValue("4"));
  EXPECT_CALL(onNext_, onCancelled());

  auto callbackHandle = processValues(std::move(transformedReceiver));

  sender.write(1);
  sender.write(2);
  executor_.drain();

  std::move(sender).close(std::runtime_error("Error"));
  std::tie(untransformedReceiver, sender) = Channel<int>::create();
  executor_.drain();

  sender.write(3);
  sender.write(4);
  executor_.drain();

  callbackHandle.reset();
  executor_.drain();
}

TEST_F(ResumableTransformFixture, TransformThrows_NoReinitialization_Rethrows) {
  auto [untransformedReceiver, sender] = Channel<int>::create();
  bool transformThrows = false;
  auto transformedReceiver = resumableTransform(
      &executor_,
      [alreadyInitialized = false, &receiver = untransformedReceiver]() mutable
      -> folly::coro::Task<std::pair<std::vector<std::string>, Receiver<int>>> {
        CHECK(!alreadyInitialized);
        alreadyInitialized = true;
        co_return std::make_pair(
            toVector(folly::to<std::string>("abc")), std::move(receiver));
      },
      [&](folly::Try<int> result)
          -> folly::coro::AsyncGenerator<std::string&&> {
        if (transformThrows) {
          throw std::runtime_error("Error");
        }
        co_yield folly::to<std::string>(result.value());
      });

  EXPECT_CALL(onNext_, onValue("abc"));
  EXPECT_CALL(onNext_, onValue("1"));
  EXPECT_CALL(onNext_, onValue("2"));
  EXPECT_CALL(onNext_, onRuntimeError("std::runtime_error: Error"));

  auto callbackHandle = processValues(std::move(transformedReceiver));

  sender.write(1);
  sender.write(2);
  executor_.drain();

  transformThrows = true;
  sender.write(100);
  executor_.drain();
}

class ResumableTransformFixtureStress : public Test {
 protected:
  ResumableTransformFixtureStress()
      : consumer_(std::make_unique<StressTestConsumer<std::string>>(
            ConsumptionMode::CallbackWithHandle,
            [lastReceived = -1](std::string value) mutable {
              if (value == "start") {
                lastReceived = -1;
              } else {
                EXPECT_EQ(folly::to<int>(value), ++lastReceived);
              }
            })) {}

  std::unique_ptr<StressTestProducer<int>> makeProducer() {
    return std::make_unique<StressTestProducer<int>>(
        [value = 0]() mutable { return value++; });
  }

  void setProducer(std::unique_ptr<StressTestProducer<int>> producer) {
    producer_ = std::move(producer);
    producerReady_.post();
  }

  void waitForProducer() {
    producerReady_.wait();
    LOG(INFO) << "Finished waiting!";
    producerReady_.reset();
  }

  StressTestProducer<int>* getProducer() { return producer_.get(); }

  static constexpr std::chrono::milliseconds kTestTimeout =
      std::chrono::milliseconds{10};

  std::unique_ptr<StressTestProducer<int>> producer_;
  folly::Baton<> producerReady_;
  std::unique_ptr<StressTestConsumer<std::string>> consumer_;
};

TEST_F(ResumableTransformFixtureStress, Close) {
  folly::CPUThreadPoolExecutor transformExecutor(1);
  bool close = false;
  consumer_->startConsuming(resumableTransform(
      folly::SerialExecutor::create(&transformExecutor),
      [&]() -> folly::coro::Task<
                std::pair<std::vector<std::string>, Receiver<int>>> {
        if (close) {
          throw OnClosedException();
        }
        auto [receiver, sender] = Channel<int>::create();
        auto newProducer = makeProducer();
        newProducer->startProducing(
            std::move(sender), std::nullopt /* closeEx */);
        setProducer(std::move(newProducer));
        co_return std::make_pair(toVector("start"s), std::move(receiver));
      },
      [](folly::Try<int> result) -> folly::coro::AsyncGenerator<std::string> {
        co_yield folly::to<std::string>(std::move(result.value()));
      }));

  waitForProducer();
  /* sleep override */
  std::this_thread::sleep_for(kTestTimeout / 2);
  getProducer()->stopProducing();

  waitForProducer();
  /* sleep override */
  std::this_thread::sleep_for(kTestTimeout / 2);
  close = true;
  getProducer()->stopProducing();

  EXPECT_EQ(consumer_->waitForClose().get(), CloseType::NoException);
}

TEST_F(ResumableTransformFixtureStress, CancelDuringReinitialization) {
  folly::CPUThreadPoolExecutor transformExecutor(1);
  auto initializationStarted = folly::SharedPromise<folly::Unit>();
  auto initializationWait = folly::SharedPromise<folly::Unit>();
  auto resumableTransformDestroyed = folly::SharedPromise<folly::Unit>();
  auto guard =
      folly::makeGuard([&]() { resumableTransformDestroyed.setValue(); });
  consumer_->startConsuming(resumableTransform(
      folly::SerialExecutor::create(&transformExecutor),
      [&, g = std::move(guard)]()
          -> folly::coro::Task<
              std::pair<std::vector<std::string>, Receiver<int>>> {
        initializationStarted.setValue(folly::unit);
        co_await folly::coro::detachOnCancel(
            initializationWait.getSemiFuture());
        initializationWait = folly::SharedPromise<folly::Unit>();
        auto [receiver, sender] = Channel<int>::create();
        auto newProducer = makeProducer();
        newProducer->startProducing(
            std::move(sender), std::nullopt /* closeEx */);
        setProducer(std::move(newProducer));
        co_return std::make_pair(toVector("start"s), std::move(receiver));
      },
      [](folly::Try<int> result) -> folly::coro::AsyncGenerator<std::string> {
        co_yield folly::to<std::string>(std::move(result.value()));
      }));

  initializationStarted.getSemiFuture().get();
  initializationStarted = folly::SharedPromise<folly::Unit>();
  initializationWait.setValue(folly::unit);
  waitForProducer();
  /* sleep override */
  std::this_thread::sleep_for(kTestTimeout / 2);
  getProducer()->stopProducing();

  initializationStarted.getSemiFuture().get();
  initializationStarted = folly::SharedPromise<folly::Unit>();
  consumer_->cancel();
  initializationWait.setValue(folly::unit);

  EXPECT_EQ(consumer_->waitForClose().get(), CloseType::Cancelled);
  resumableTransformDestroyed.getSemiFuture().get();
}
} // namespace channels
} // namespace folly
