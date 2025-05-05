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

#include <folly/executors/ManualExecutor.h>
#include <folly/executors/SerialExecutor.h>
#include <folly/experimental/channels/ConsumeChannel.h>
#include <folly/experimental/channels/FanoutSender.h>
#include <folly/experimental/channels/test/ChannelTestUtil.h>
#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>

namespace folly {
namespace channels {

using namespace testing;

class FanoutSenderFixture : public Test {
 protected:
  FanoutSenderFixture() {}

  using TCallback = StrictMock<MockNextCallback<int>>;

  std::pair<ChannelCallbackHandle, TCallback*> processValues(
      Receiver<int> receiver) {
    auto callback = std::make_unique<TCallback>();
    auto callbackPtr = callback.get();
    auto handle = consumeChannelWithCallback(
        std::move(receiver),
        &executor_,
        [cbk = std::move(callback)](
            Try<int> resultTry) mutable -> folly::coro::Task<bool> {
          (*cbk)(std::move(resultTry));
          co_return true;
        });
    return std::make_pair(std::move(handle), callbackPtr);
  }

  StrictMock<MockNextCallback<std::string>> createCallback() {
    return StrictMock<MockNextCallback<std::string>>();
  }

  folly::ManualExecutor executor_;
};

TEST_F(FanoutSenderFixture, WriteValue_FanoutBroadcastsValues) {
  auto fanoutSender = FanoutSender<int>();

  auto [handle1, callback1] =
      processValues(fanoutSender.subscribe(toVector(100)));
  auto [handle2, callback2] =
      processValues(fanoutSender.subscribe(toVector(200)));

  EXPECT_CALL(*callback1, onValue(100));
  EXPECT_CALL(*callback2, onValue(200));
  EXPECT_CALL(*callback1, onValue(1));
  EXPECT_CALL(*callback2, onValue(1));
  EXPECT_CALL(*callback1, onValue(2));
  EXPECT_CALL(*callback2, onValue(2));
  EXPECT_CALL(*callback1, onClosed());
  EXPECT_CALL(*callback2, onClosed());

  executor_.drain();

  EXPECT_TRUE(fanoutSender.anySubscribers());

  fanoutSender.write(1);
  fanoutSender.write(2);
  executor_.drain();

  std::move(fanoutSender).close();
  executor_.drain();
}

TEST_F(FanoutSenderFixture, InputThrows_AllOutputReceiversGetException) {
  auto fanoutSender = FanoutSender<int>();

  auto [handle1, callback1] = processValues(fanoutSender.subscribe());
  auto [handle2, callback2] = processValues(fanoutSender.subscribe());

  EXPECT_CALL(*callback1, onValue(1));
  EXPECT_CALL(*callback2, onValue(1));
  EXPECT_CALL(*callback1, onRuntimeError("std::runtime_error: Error"));
  EXPECT_CALL(*callback2, onRuntimeError("std::runtime_error: Error"));

  executor_.drain();

  EXPECT_TRUE(fanoutSender.anySubscribers());

  fanoutSender.write(1);
  executor_.drain();

  std::move(fanoutSender).close(std::runtime_error("Error"));
  executor_.drain();
}

TEST_F(FanoutSenderFixture, ReceiversCancelled) {
  auto fanoutSender = FanoutSender<int>();

  auto [handle1, callback1] = processValues(fanoutSender.subscribe());
  auto [handle2, callback2] = processValues(fanoutSender.subscribe());

  EXPECT_CALL(*callback1, onValue(1));
  EXPECT_CALL(*callback2, onValue(1));
  EXPECT_CALL(*callback1, onCancelled());
  EXPECT_CALL(*callback2, onValue(2));
  EXPECT_CALL(*callback2, onCancelled());

  executor_.drain();

  EXPECT_TRUE(fanoutSender.anySubscribers());

  fanoutSender.write(1);
  executor_.drain();

  EXPECT_TRUE(fanoutSender.anySubscribers());

  handle1.reset();
  fanoutSender.write(2);
  executor_.drain();

  EXPECT_TRUE(fanoutSender.anySubscribers());

  handle2.reset();
  fanoutSender.write(3);
  executor_.drain();

  EXPECT_FALSE(fanoutSender.anySubscribers());

  std::move(fanoutSender).close();
  executor_.drain();
}

TEST_F(FanoutSenderFixture, NumSubscribers) {
  auto sender = FanoutSender<int>{};
  EXPECT_EQ(sender.numSubscribers(), 0);

  auto receiver1 = std::make_unique<Receiver<int>>(sender.subscribe());
  EXPECT_EQ(sender.numSubscribers(), 1);

  auto receiver2 = std::make_unique<Receiver<int>>(sender.subscribe());
  EXPECT_EQ(sender.numSubscribers(), 2);

  auto receiver3 = std::make_unique<Receiver<int>>(sender.subscribe());
  EXPECT_EQ(sender.numSubscribers(), 3);
}
} // namespace channels
} // namespace folly
