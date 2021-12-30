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

#pragma once

#include <folly/executors/SequencedExecutor.h>
#include <folly/experimental/channels/ChannelCallbackHandle.h>
#include <folly/experimental/coro/Task.h>

namespace folly {
namespace channels {

/**
 * A Producer is a base class for an object that produces a channel. The
 * subclass can call write to write a new value to the channel, and close to
 * close the channel. It is a useful way to generate output values for a
 * receiver, without having to keep alive an extraneous object that produces
 * those values.
 *
 * When the consumer of the channel stops consuming, the onClosed function will
 * be called. The subclass should cancel any ongoing work in this function.
 * After onCancelled is called, the object will be deleted once the last
 * outstanding KeepAlive is destroyed.
 *
 * Example:
 *   // Function that returns an executor
 *   folly::Executor::KeepAlive<folly::SequencedExecutor> getExecutor();
 *
 *   // Function that returns output values
 *   std::vector<int> getLatestOutputValues();
 *
 *   // Example producer implementation
 *   class PollingProducer : public Producer<int> {
 *    public:
 *     PollingProducer(
 *         Sender<int> sender,
 *         folly::Executor::KeepAlive<folly::SequencedExecutor> executor)
 *         : Producer<int>(std::move(sender), std::move(executor)) {
 *       // Start polling for values.
 *       folly::coro::co_withCancellation(
 *             cancelSource_.getToken(),
 *             [=, keepAlive = getKeepAlive()]() {
 *               return pollForOutputValues();
 *             })
 *         .scheduleOn(getExecutor())
 *         .start();
 *     }
 *
 *     folly::coro::Task<void> onClosed() override {
 *       // The consumer has stopped consuming our values. Stop polling.
 *       cancelSource_.requestCancellation();
 *     }
 *
 *    private:
 *     folly::coro::Task<void> pollForOutputValues() {
 *       auto cancelToken = co_await folly::coro::co_current_cancellation_token;
 *       while (!cancelToken.isCancellationRequested()) {
 *         auto outputValues = getLatestOutputValues();
 *         for (auto& outputValue : outputValues) {
 *           write(std::move(outputValue));
 *         }
 *       }
 *       co_await folly::coro::sleep(std::chrono::seconds(1));
 *     }
 *
 *     folly::CancellationSource cancelSource_;
 *   };
 *
 *   // Producer usage
 *   Receiver<int> receiver = makeProducer<PollingProducer>(getExecutor());
 */
template <typename TValue>
class Producer : public detail::IChannelCallback {
 public:
  using ValueType = TValue;

 protected:
  /**
   * This object will ensure that the corresponding Producer that created it
   * will not be destroyed.
   */
  class KeepAlive {
   public:
    ~KeepAlive();
    KeepAlive(KeepAlive&&) noexcept;
    KeepAlive& operator=(KeepAlive&&) noexcept;

   private:
    friend class Producer<TValue>;

    explicit KeepAlive(Producer<TValue>* ptr);

    Producer<TValue>* ptr_;
  };

  Producer(
      Sender<TValue> sender,
      folly::Executor::KeepAlive<folly::SequencedExecutor> executor);
  virtual ~Producer() override = default;

  /**
   * Writes a value into the channel.
   */
  void write(TValue value);

  /**
   * Closes the channel.
   */
  void close(std::optional<folly::exception_wrapper> ex = std::nullopt);

  /**
   * Returns whether or not this producer is closed or cancelled.
   */
  bool isClosed();

  /**
   * Returns the executor used for this producer.
   */
  folly::Executor::KeepAlive<folly::SequencedExecutor> getExecutor();

  /**
   * Returns a KeepAlive object. This object will not be destroyed before all
   * KeepAlive objects are destroyed.
   */
  KeepAlive getKeepAlive();

  /**
   * Called when the corresponding receiver is cancelled, or the sender is
   * closed.
   */
  virtual folly::coro::Task<void> onClosed() { co_return; }

  /**
   * If you get an error that this function is not implemented, do not
   * implement it. Instead, create your object with makeProducer
   * below.
   */
  virtual void ensureMakeProducerUsedForCreation() = 0;

 private:
  template <typename TProducer, typename... Args>
  friend Receiver<typename TProducer::ValueType> makeProducer(
      folly::Executor::KeepAlive<folly::SequencedExecutor> executor,
      Args&&... args);

  void consume(detail::ChannelBridgeBase* bridge) override;

  void canceled(detail::ChannelBridgeBase* bridge) override;

  detail::ChannelBridgePtr<TValue> sender_;
  folly::Executor::KeepAlive<folly::SequencedExecutor> executor_;
  std::atomic<int> refCount_{1};
};

/**
 * Creates a new object that extends the Producer class, and returns a receiver.
 * The receiver will receive any values produced by the producer. See the
 * description of the Producer class for information on how to implement a
 * producer.
 */
template <typename TProducer, typename... Args>
Receiver<typename TProducer::ValueType> makeProducer(
    folly::Executor::KeepAlive<folly::SequencedExecutor> executor,
    Args&&... args);
} // namespace channels
} // namespace folly

#include <folly/experimental/channels/Producer-inl.h>
