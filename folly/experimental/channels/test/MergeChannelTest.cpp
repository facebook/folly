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

#include <folly/executors/ManualExecutor.h>
#include <folly/executors/SerialExecutor.h>
#include <folly/experimental/channels/ConsumeChannel.h>
#include <folly/experimental/channels/MergeChannel.h>
#include <folly/experimental/channels/test/ChannelTestUtil.h>
#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>

namespace folly {
namespace channels {

using namespace testing;

class MergeChannelFixture : public Test {
 protected:
  MergeChannelFixture() {}

  ~MergeChannelFixture() { executor_.drain(); }

  using TCallback = StrictMock<MockNextCallback<int>>;

  ChannelCallbackHandle processValues(Receiver<int> receiver) {
    return consumeChannelWithCallback(
        std::move(receiver),
        &executor_,
        [=](folly::Try<int> resultTry) -> folly::coro::Task<bool> {
          onNext_(std::move(resultTry));
          co_return true;
        });
  }

  folly::ManualExecutor executor_;
  StrictMock<MockNextCallback<int>> onNext_;
};

TEST_F(MergeChannelFixture, ReceiveValues_ReturnMergedValues) {
  auto [receiver1, sender1] = Channel<int>::create();
  auto [receiver2, sender2] = Channel<int>::create();
  auto [receiver3, sender3] = Channel<int>::create();
  auto [mergedReceiver, mergeChannel] =
      createMergeChannel<int, std::string>(&executor_);

  mergeChannel.addNewReceiver("sub1", std::move(receiver1));
  mergeChannel.addNewReceiver("sub2", std::move(receiver2));
  executor_.drain();

  EXPECT_CALL(onNext_, onValue(1));
  EXPECT_CALL(onNext_, onValue(2));
  EXPECT_CALL(onNext_, onValue(3));
  EXPECT_CALL(onNext_, onValue(5));
  EXPECT_CALL(onNext_, onClosed());

  auto callbackHandle = processValues(std::move(mergedReceiver));

  sender1.write(1);
  sender2.write(2);
  executor_.drain();

  mergeChannel.addNewReceiver("sub3", std::move(receiver3));
  mergeChannel.removeReceiver("sub2");

  sender1.write(3);
  sender2.write(4);
  sender3.write(5);
  executor_.drain();

  std::move(mergeChannel).close();
  executor_.drain();
}

TEST_F(
    MergeChannelFixture,
    ReceiveValues_NewInputReceiver_WithSameSubscriptionId) {
  auto [receiver1, sender1] = Channel<int>::create();
  auto [receiver2a, sender2a] = Channel<int>::create();
  auto [receiver2b, sender2b] = Channel<int>::create();
  auto [mergedReceiver, mergeChannel] =
      createMergeChannel<int, std::string>(&executor_);

  mergeChannel.addNewReceiver("sub1", std::move(receiver1));
  mergeChannel.addNewReceiver("sub2", std::move(receiver2a));
  executor_.drain();

  EXPECT_CALL(onNext_, onValue(1));
  EXPECT_CALL(onNext_, onValue(2));
  EXPECT_CALL(onNext_, onValue(3));
  EXPECT_CALL(onNext_, onValue(5));
  EXPECT_CALL(onNext_, onClosed());

  auto callbackHandle = processValues(std::move(mergedReceiver));

  sender1.write(1);
  sender2a.write(2);
  executor_.drain();

  mergeChannel.addNewReceiver("sub2", std::move(receiver2b));

  sender1.write(3);
  sender2a.write(4);
  sender2b.write(5);
  executor_.drain();

  std::move(mergeChannel).close();
  executor_.drain();
}

TEST_F(
    MergeChannelFixture,
    ReceiveValues_NewInputReceiver_WithSameSubscriptionId_AfterClose) {
  auto [receiver1, sender1] = Channel<int>::create();
  auto [receiver2a, sender2a] = Channel<int>::create();
  auto [receiver2b, sender2b] = Channel<int>::create();
  auto [mergedReceiver, mergeChannel] =
      createMergeChannel<int, std::string>(&executor_);

  mergeChannel.addNewReceiver("sub1", std::move(receiver1));
  mergeChannel.addNewReceiver("sub2", std::move(receiver2a));
  executor_.drain();

  EXPECT_CALL(onNext_, onValue(1));
  EXPECT_CALL(onNext_, onValue(2));
  EXPECT_CALL(onNext_, onValue(3));
  EXPECT_CALL(onNext_, onValue(5));
  EXPECT_CALL(onNext_, onClosed());

  auto callbackHandle = processValues(std::move(mergedReceiver));

  sender1.write(1);
  sender2a.write(2);
  executor_.drain();

  std::move(sender2a).close();
  executor_.drain();

  mergeChannel.addNewReceiver("sub2", std::move(receiver2b));
  executor_.drain();

  sender1.write(3);
  sender2b.write(5);
  executor_.drain();

  std::move(mergeChannel).close();
  executor_.drain();
}

TEST_F(MergeChannelFixture, ReceiveValues_RemoveReceiver) {
  auto [receiver1, sender1] = Channel<int>::create();
  auto [receiver2, sender2] = Channel<int>::create();
  auto [mergedReceiver, mergeChannel] =
      createMergeChannel<int, std::string>(&executor_);

  mergeChannel.addNewReceiver("sub1", std::move(receiver1));
  mergeChannel.addNewReceiver("sub2", std::move(receiver2));
  executor_.drain();

  EXPECT_CALL(onNext_, onValue(1));
  EXPECT_CALL(onNext_, onValue(2));
  EXPECT_CALL(onNext_, onValue(3));
  EXPECT_CALL(onNext_, onClosed());

  auto callbackHandle = processValues(std::move(mergedReceiver));

  sender1.write(1);
  sender2.write(2);
  executor_.drain();

  mergeChannel.removeReceiver("sub2");

  sender1.write(3);
  sender2.write(4);
  executor_.drain();

  std::move(mergeChannel).close();
  executor_.drain();
}

TEST_F(MergeChannelFixture, ReceiveValues_RemoveReceiver_AfterClose) {
  auto [receiver1, sender1] = Channel<int>::create();
  auto [receiver2, sender2] = Channel<int>::create();
  auto [mergedReceiver, mergeChannel] =
      createMergeChannel<int, std::string>(&executor_);

  mergeChannel.addNewReceiver("sub1", std::move(receiver1));
  mergeChannel.addNewReceiver("sub2", std::move(receiver2));
  executor_.drain();

  EXPECT_CALL(onNext_, onValue(1));
  EXPECT_CALL(onNext_, onValue(2));
  EXPECT_CALL(onNext_, onValue(3));
  EXPECT_CALL(onNext_, onClosed());

  auto callbackHandle = processValues(std::move(mergedReceiver));

  sender1.write(1);
  sender2.write(2);
  executor_.drain();

  std::move(sender2).close();
  executor_.drain();

  mergeChannel.removeReceiver("sub2");
  sender1.write(3);
  executor_.drain();

  std::move(mergeChannel).close();
  executor_.drain();
}

TEST_F(MergeChannelFixture, OneInputClosed_ContinuesMerging) {
  auto [receiver1, sender1] = Channel<int>::create();
  auto [receiver2, sender2] = Channel<int>::create();
  auto [receiver3, sender3] = Channel<int>::create();
  auto [mergedReceiver, mergeChannel] =
      createMergeChannel<int, std::string>(&executor_);

  mergeChannel.addNewReceiver("sub1", std::move(receiver1));
  mergeChannel.addNewReceiver("sub2", std::move(receiver2));
  mergeChannel.addNewReceiver("sub3", std::move(receiver3));
  executor_.drain();

  EXPECT_CALL(onNext_, onValue(1));
  EXPECT_CALL(onNext_, onValue(2));
  EXPECT_CALL(onNext_, onValue(3));
  EXPECT_CALL(onNext_, onValue(4));
  EXPECT_CALL(onNext_, onValue(5));
  EXPECT_CALL(onNext_, onClosed());

  auto callbackHandle = processValues(std::move(mergedReceiver));

  sender1.write(1);
  sender2.write(2);
  sender3.write(3);
  std::move(sender3).close();
  executor_.drain();

  sender1.write(4);
  sender2.write(5);
  executor_.drain();

  std::move(sender1).close();
  std::move(sender2).close();
  executor_.drain();

  std::move(mergeChannel).close();
  executor_.drain();
}

TEST_F(MergeChannelFixture, OneInputThrows_OutputClosedWithException) {
  auto [receiver1, sender1] = Channel<int>::create();
  auto [receiver2, sender2] = Channel<int>::create();
  auto [receiver3, sender3] = Channel<int>::create();
  auto [mergedReceiver, mergeChannel] =
      createMergeChannel<int, std::string>(&executor_);

  mergeChannel.addNewReceiver("sub1", std::move(receiver1));
  mergeChannel.addNewReceiver("sub2", std::move(receiver2));
  mergeChannel.addNewReceiver("sub3", std::move(receiver3));
  executor_.drain();

  EXPECT_CALL(onNext_, onValue(1));
  EXPECT_CALL(onNext_, onValue(2));
  EXPECT_CALL(onNext_, onValue(3));
  EXPECT_CALL(onNext_, onRuntimeError("std::runtime_error: Error"));

  auto callbackHandle = processValues(std::move(mergedReceiver));

  sender1.write(1);
  sender2.write(2);
  sender3.write(3);
  std::move(sender3).close(std::runtime_error("Error"));
  executor_.drain();

  sender1.write(4);
  sender2.write(5);
  std::move(sender1).close();
  std::move(sender2).close();
  executor_.drain();
}

TEST_F(MergeChannelFixture, Cancelled) {
  auto [receiver1, sender1] = Channel<int>::create();
  auto [receiver2, sender2] = Channel<int>::create();
  auto [receiver3, sender3] = Channel<int>::create();
  auto [mergedReceiver, mergeChannel] =
      createMergeChannel<int, std::string>(&executor_);

  mergeChannel.addNewReceiver("sub1", std::move(receiver1));
  mergeChannel.addNewReceiver("sub2", std::move(receiver2));
  mergeChannel.addNewReceiver("sub3", std::move(receiver3));
  executor_.drain();

  EXPECT_CALL(onNext_, onValue(1));
  EXPECT_CALL(onNext_, onValue(2));
  EXPECT_CALL(onNext_, onValue(3));
  EXPECT_CALL(onNext_, onCancelled());

  auto callbackHandle = processValues(std::move(mergedReceiver));

  sender1.write(1);
  sender2.write(2);
  sender3.write(3);
  executor_.drain();

  callbackHandle.reset();
  executor_.drain();

  sender1.write(4);
  sender2.write(5);
  sender3.write(6);
  executor_.drain();

  std::move(sender1).close();
  std::move(sender2).close();
  std::move(sender3).close();
  executor_.drain();
}

struct ProducedValue {
  int producerIndex;
  int value;
};

class MergeChannelFixtureStress : public Test {
 protected:
  MergeChannelFixtureStress()
      : producers_(toVector(makeProducer(0), makeProducer(1))),
        consumer_(makeConsumer()) {}

  static std::unique_ptr<StressTestProducer<ProducedValue>> makeProducer(
      int index) {
    return std::make_unique<StressTestProducer<ProducedValue>>(
        [index, value = 0]() mutable {
          return ProducedValue{index, value++};
        });
  }

  static std::unique_ptr<StressTestConsumer<ProducedValue>> makeConsumer() {
    return std::make_unique<StressTestConsumer<ProducedValue>>(
        ConsumptionMode::CallbackWithHandle,
        [lastReceived = toVector(-1, -1, -1)](ProducedValue value) mutable {
          EXPECT_EQ(value.value, ++lastReceived[value.producerIndex]);
        });
  }

  static void sleepFor(std::chrono::milliseconds duration) {
    /* sleep override */
    std::this_thread::sleep_for(duration);
  }

  static constexpr std::chrono::milliseconds kTestTimeout =
      std::chrono::milliseconds{5000};

  std::vector<std::unique_ptr<StressTestProducer<ProducedValue>>> producers_;
  std::unique_ptr<StressTestConsumer<ProducedValue>> consumer_;
};

TEST_F(MergeChannelFixtureStress, HandleClosed) {
  folly::CPUThreadPoolExecutor mergeChannelExecutor(1);
  auto [mergeReceiver, mergeChannel] = createMergeChannel<ProducedValue, int>(
      folly::SerialExecutor::create(&mergeChannelExecutor));
  consumer_->startConsuming(std::move(mergeReceiver));

  auto [receiver0, sender0] = Channel<ProducedValue>::create();
  producers_.at(0)->startProducing(std::move(sender0), std::nullopt);
  mergeChannel.addNewReceiver(0 /* subscriptionId */, std::move(receiver0));

  sleepFor(kTestTimeout / 3);

  auto [receiver1, sender1] = Channel<ProducedValue>::create();
  producers_.at(1)->startProducing(std::move(sender1), std::nullopt);
  mergeChannel.addNewReceiver(1 /* subscriptionId */, std::move(receiver1));

  sleepFor(kTestTimeout / 3);

  mergeChannel.removeReceiver(0 /* subscriptionId */);

  sleepFor(kTestTimeout / 3);

  std::move(mergeChannel).close();
  EXPECT_EQ(consumer_->waitForClose().get(), CloseType::NoException);
}

TEST_F(MergeChannelFixtureStress, InputChannelReceivesException) {
  folly::CPUThreadPoolExecutor mergeChannelExecutor(1);
  auto [mergeReceiver, mergeChannel] = createMergeChannel<ProducedValue, int>(
      folly::SerialExecutor::create(&mergeChannelExecutor));
  consumer_->startConsuming(std::move(mergeReceiver));

  auto [receiver0, sender0] = Channel<ProducedValue>::create();
  producers_.at(0)->startProducing(
      std::move(sender0), std::runtime_error("Error"));
  mergeChannel.addNewReceiver(0 /* subscriptionId */, std::move(receiver0));

  sleepFor(kTestTimeout / 4);

  auto [receiver1, sender1] = Channel<ProducedValue>::create();
  producers_.at(1)->startProducing(
      std::move(sender1), std::runtime_error("Error"));
  mergeChannel.addNewReceiver(1 /* subscriptionId */, std::move(receiver1));

  sleepFor(kTestTimeout / 4);

  mergeChannel.removeReceiver(0 /* subscriptionId */);

  sleepFor(kTestTimeout / 4);

  producers_.at(0)->stopProducing();

  sleepFor(kTestTimeout / 4);

  auto closeFuture = consumer_->waitForClose();
  EXPECT_FALSE(closeFuture.isReady());
  producers_.at(1)->stopProducing();
  EXPECT_EQ(std::move(closeFuture).get(), CloseType::Exception);
}

TEST_F(MergeChannelFixtureStress, Cancelled) {
  folly::CPUThreadPoolExecutor mergeChannelExecutor(1);
  auto [mergeReceiver, mergeChannel] = createMergeChannel<ProducedValue, int>(
      folly::SerialExecutor::create(&mergeChannelExecutor));
  consumer_->startConsuming(std::move(mergeReceiver));

  auto [receiver0, sender0] = Channel<ProducedValue>::create();
  producers_.at(0)->startProducing(std::move(sender0), std::nullopt);
  mergeChannel.addNewReceiver(0 /* subscriptionId */, std::move(receiver0));

  sleepFor(kTestTimeout / 3);

  auto [receiver1, sender1] = Channel<ProducedValue>::create();
  producers_.at(1)->startProducing(std::move(sender1), std::nullopt);
  mergeChannel.addNewReceiver(1 /* subscriptionId */, std::move(receiver1));

  sleepFor(kTestTimeout / 3);

  mergeChannel.removeReceiver(0 /* subscriptionId */);

  sleepFor(kTestTimeout / 3);

  consumer_->cancel();
  EXPECT_EQ(consumer_->waitForClose().get(), CloseType::Cancelled);
}

TEST_F(MergeChannelFixtureStress, Cancelled_ThenHandleClosedImmediately) {
  folly::CPUThreadPoolExecutor mergeChannelExecutor(1);
  auto [mergeReceiver, mergeChannel] = createMergeChannel<ProducedValue, int>(
      folly::SerialExecutor::create(&mergeChannelExecutor));
  consumer_->startConsuming(std::move(mergeReceiver));

  auto [receiver0, sender0] = Channel<ProducedValue>::create();
  producers_.at(0)->startProducing(std::move(sender0), std::nullopt);
  mergeChannel.addNewReceiver(0 /* subscriptionId */, std::move(receiver0));

  sleepFor(kTestTimeout / 3);

  auto [receiver1, sender1] = Channel<ProducedValue>::create();
  producers_.at(1)->startProducing(std::move(sender1), std::nullopt);
  mergeChannel.addNewReceiver(1 /* subscriptionId */, std::move(receiver1));

  sleepFor(kTestTimeout / 3);

  mergeChannel.removeReceiver(0 /* subscriptionId */);

  sleepFor(kTestTimeout / 3);

  consumer_->cancel();
  std::move(mergeChannel).close();
  EXPECT_THAT(
      consumer_->waitForClose().get(),
      AnyOf(Eq(CloseType::NoException), Eq(CloseType::Cancelled)));
}

TEST_F(MergeChannelFixtureStress, HandleClosed_ThenCancelledImmediately) {
  folly::CPUThreadPoolExecutor mergeChannelExecutor(1);
  auto [mergeReceiver, mergeChannel] = createMergeChannel<ProducedValue, int>(
      folly::SerialExecutor::create(&mergeChannelExecutor));
  consumer_->startConsuming(std::move(mergeReceiver));

  auto [receiver0, sender0] = Channel<ProducedValue>::create();
  producers_.at(0)->startProducing(std::move(sender0), std::nullopt);
  mergeChannel.addNewReceiver(0 /* subscriptionId */, std::move(receiver0));

  sleepFor(kTestTimeout / 3);

  auto [receiver1, sender1] = Channel<ProducedValue>::create();
  producers_.at(1)->startProducing(std::move(sender1), std::nullopt);
  mergeChannel.addNewReceiver(1 /* subscriptionId */, std::move(receiver1));

  sleepFor(kTestTimeout / 3);

  mergeChannel.removeReceiver(0 /* subscriptionId */);

  sleepFor(kTestTimeout / 3);

  std::move(mergeChannel).close();
  consumer_->cancel();
  EXPECT_THAT(
      consumer_->waitForClose().get(),
      AnyOf(Eq(CloseType::NoException), Eq(CloseType::Cancelled)));
}
} // namespace channels
} // namespace folly
