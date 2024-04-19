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
#include <folly/experimental/channels/ChannelProcessor.h>
#include <folly/portability/GMock.h>

namespace folly {
namespace channels {

using namespace testing;

struct MockCallback {
  MOCK_METHOD2(onInputValue, void(const std::string& key, int value));
};

class ChannelProcessorFixture : public Test {
 protected:
  ChannelProcessorFixture()
      : processor_(createChannelProcessor<std::string>(&executor_)) {}

  folly::ManualExecutor executor_;
  ChannelProcessor<std::string> processor_;
  StrictMock<MockCallback> callback_;
};

TEST_F(ChannelProcessorFixture, SimpleChannel_ReceiveValues) {
  auto [receiver, sender] = Channel<int>::create();
  processor_.addChannel(
      "one",
      std::move(receiver),
      [&](Try<int> value) -> folly::coro::Task<void> {
        callback_.onInputValue("one", *value);
        co_return;
      });

  executor_.drain();

  EXPECT_CALL(callback_, onInputValue("one", 1));
  EXPECT_CALL(callback_, onInputValue("one", 2));
  EXPECT_CALL(callback_, onInputValue("one", 3));

  sender.write(1);
  sender.write(2);
  sender.write(3);
  executor_.drain();
}

TEST_F(ChannelProcessorFixture, SimpleChannel_ThrowOnClosedException) {
  auto [receiver, sender] = Channel<int>::create();
  processor_.addChannel(
      "one",
      std::move(receiver),
      [&](Try<int> value) -> folly::coro::Task<void> {
        if (*value == -1) {
          throw folly::channels::OnClosedException();
        }
        callback_.onInputValue("one", *value);
        co_return;
      });

  executor_.drain();

  EXPECT_CALL(callback_, onInputValue("one", 1));
  EXPECT_CALL(callback_, onInputValue("one", 2));
  EXPECT_CALL(callback_, onInputValue("one", 3));

  sender.write(1);
  sender.write(2);
  sender.write(3);
  sender.write(-1);
  sender.write(4);
  executor_.drain();
}

TEST_F(
    ChannelProcessorFixture, SimpleChannel_ThrowOperationCancelledException) {
  auto [receiver, sender] = Channel<int>::create();
  processor_.addChannel(
      "one",
      std::move(receiver),
      [&](Try<int> value) -> folly::coro::Task<void> {
        if (*value == -1) {
          throw folly::OperationCancelled();
        }
        callback_.onInputValue("one", *value);
        co_return;
      });

  executor_.drain();

  EXPECT_CALL(callback_, onInputValue("one", 1));
  EXPECT_CALL(callback_, onInputValue("one", 2));
  EXPECT_CALL(callback_, onInputValue("one", 3));

  sender.write(1);
  sender.write(2);
  sender.write(3);
  sender.write(-1);
  sender.write(4);
  executor_.drain();
}

TEST_F(ChannelProcessorFixture, SimpleChannel_ThrowOtherException_Death) {
  auto [receiver, sender] = Channel<int>::create();
  processor_.addChannel(
      "one",
      std::move(receiver),
      [&](Try<int> value) -> folly::coro::Task<void> {
        if (*value == -1) {
          throw std::runtime_error("Unhandled exception");
        }
        callback_.onInputValue("one", *value);
        co_return;
      });

  executor_.drain();

  EXPECT_CALL(callback_, onInputValue("one", 1));
  EXPECT_CALL(callback_, onInputValue("one", 2));
  EXPECT_CALL(callback_, onInputValue("one", 3));

  sender.write(1);
  sender.write(2);
  sender.write(3);
  executor_.drain();

  EXPECT_DEATH(
      {
        sender.write(-1);
        executor_.drain();
      },
      "Unhandled exception");
}

TEST_F(ChannelProcessorFixture, SimpleChannel_RemoveChannel) {
  auto [receiver, sender] = Channel<int>::create();
  auto [promise, future] = folly::coro::makePromiseContract<void>();
  bool waitForFuture = false;
  bool cancelled = false;
  processor_.addChannel(
      "one",
      std::move(receiver),
      [&, &future_2 = future](Try<int> value) -> folly::coro::Task<void> {
        if (waitForFuture) {
          try {
            co_await std::move(future_2);
          } catch (const folly::OperationCancelled&) {
            cancelled = true;
            throw;
          }
        }
        callback_.onInputValue("one", *value);
        co_return;
      });

  executor_.drain();

  EXPECT_CALL(callback_, onInputValue("one", 1));
  EXPECT_CALL(callback_, onInputValue("one", 2));
  EXPECT_CALL(callback_, onInputValue("one", 3));

  sender.write(1);
  sender.write(2);
  sender.write(3);
  executor_.drain();

  waitForFuture = true;
  sender.write(4);
  executor_.drain();

  processor_.removeChannel("one");
  promise.setValue();
  executor_.drain();

  EXPECT_TRUE(cancelled);
}

TEST_F(
    ChannelProcessorFixture, ResumableChannel_ReceiveValues_ThenReinitializes) {
  struct ReceiverInfo {
    size_t index;
  };

  struct State {
    size_t receiverIndex;
  };

  auto receivers = std::vector<Receiver<int>>();
  auto [receiver0, sender0] = Channel<int>::create();
  auto [receiver1, sender1] = Channel<int>::create();
  receivers.push_back(std::move(receiver0));
  receivers.push_back(std::move(receiver1));
  processor_.addResumableChannelWithState(
      "one",
      ReceiverInfo{0},
      [&](ReceiverInfo receiverInfo,
          State& state) -> folly::coro::Task<Receiver<int>> {
        state.receiverIndex = receiverInfo.index;
        co_return std::move(receivers[receiverInfo.index]);
      },
      [&](Try<int> value, State& state) -> folly::coro::Task<void> {
        if (*value == -1) {
          state.receiverIndex++;
          throw ReinitializeException(ReceiverInfo{state.receiverIndex});
        }
        callback_.onInputValue("one", *value);
        co_return;
      },
      State());

  executor_.drain();

  EXPECT_CALL(callback_, onInputValue("one", 1));
  EXPECT_CALL(callback_, onInputValue("one", 2));
  EXPECT_CALL(callback_, onInputValue("one", 3));
  EXPECT_CALL(callback_, onInputValue("one", 4));

  sender0.write(1);
  sender0.write(2);
  sender0.write(-1);
  sender0.write(999); // Should not be received
  sender1.write(3);
  sender1.write(4);
  executor_.drain();
}

TEST_F(
    ChannelProcessorFixture,
    ResumableChannel_InitializeThrowsOnClosedException) {
  struct InitializeArg {};
  struct State {};

  bool stillProcessing = true;
  auto guard = folly::makeGuard([&] { stillProcessing = false; });

  processor_.addResumableChannelWithState(
      "one",
      InitializeArg{},
      [guard = std::move(guard)](auto, auto&)
          -> folly::coro::Task<Receiver<int>> { throw OnClosedException(); },
      [&](auto, auto&) -> folly::coro::Task<void> { co_return; },
      State());
  executor_.drain();

  EXPECT_FALSE(stillProcessing);
}

TEST_F(
    ChannelProcessorFixture,
    ResumableChannel_InitializeThrowsOperationCancelledException) {
  struct InitializeArg {};
  struct State {};

  bool stillProcessing = true;
  auto guard = folly::makeGuard([&] { stillProcessing = false; });

  processor_.addResumableChannelWithState(
      "one",
      InitializeArg{},
      [guard = std::move(guard)](auto, auto&)
          -> folly::coro::Task<Receiver<int>> { throw OperationCancelled(); },
      [&](auto, auto&) -> folly::coro::Task<void> { co_return; },
      State());
  executor_.drain();

  EXPECT_FALSE(stillProcessing);
}

TEST_F(
    ChannelProcessorFixture,
    ResumableChannel_TransformValueThrowsOnClosedException) {
  struct InitializeArg {};
  struct State {};

  int numInitializeCalls = 0;
  int numTransformCalls = 0;
  auto [receiver, sender] = Channel<int>::create();
  processor_.addResumableChannelWithState(
      "one",
      InitializeArg{},
      [&, &receiver_2 = receiver](
          auto, auto&) -> folly::coro::Task<Receiver<int>> {
        numInitializeCalls++;
        co_return std::move(receiver_2);
      },
      [&](auto, auto&) -> folly::coro::Task<void> {
        numTransformCalls++;
        throw OnClosedException();
      },
      State());

  executor_.drain();

  EXPECT_EQ(numInitializeCalls, 1);
  EXPECT_EQ(numTransformCalls, 0);

  sender.write(1);
  executor_.drain();

  EXPECT_EQ(numInitializeCalls, 1);
  EXPECT_EQ(numTransformCalls, 1);
}

TEST_F(
    ChannelProcessorFixture,
    ResumableChannel_TransformValueThrowsOperationCancelledException) {
  struct InitializeArg {};
  struct State {};

  int numInitializeCalls = 0;
  int numTransformCalls = 0;
  auto [receiver, sender] = Channel<int>::create();
  processor_.addResumableChannelWithState(
      "one",
      InitializeArg{},
      [&, &receiver_2 = receiver](
          auto, auto&) -> folly::coro::Task<Receiver<int>> {
        numInitializeCalls++;
        co_return std::move(receiver_2);
      },
      [&](auto, auto&) -> folly::coro::Task<void> {
        numTransformCalls++;
        throw OperationCancelled();
      },
      State());

  executor_.drain();

  EXPECT_EQ(numInitializeCalls, 1);
  EXPECT_EQ(numTransformCalls, 0);

  sender.write(1);
  executor_.drain();

  EXPECT_EQ(numInitializeCalls, 1);
  EXPECT_EQ(numTransformCalls, 1);
}
} // namespace channels
} // namespace folly
