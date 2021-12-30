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

#include <folly/container/F14Map.h>
#include <folly/container/F14Set.h>
#include <folly/executors/SequencedExecutor.h>
#include <folly/experimental/channels/Channel.h>
#include <folly/experimental/channels/detail/Utility.h>

namespace folly {
namespace channels {

template <typename KeyType, typename ValueType>
MergeChannel<KeyType, ValueType>::MergeChannel(TProcessor* processor)
    : processor_(processor) {}

template <typename KeyType, typename ValueType>
MergeChannel<KeyType, ValueType>::MergeChannel(MergeChannel&& other) noexcept
    : processor_(std::exchange(other.processor_, nullptr)) {}

template <typename KeyType, typename ValueType>
MergeChannel<KeyType, ValueType>& MergeChannel<KeyType, ValueType>::operator=(
    MergeChannel&& other) noexcept {
  if (&other == this) {
    return *this;
  }
  if (processor_) {
    std::move(*this).close();
  }
  processor_ = std::exchange(other.processor_, nullptr);
  return *this;
}

template <typename KeyType, typename ValueType>
MergeChannel<KeyType, ValueType>::~MergeChannel() {
  if (processor_) {
    std::move(*this).close(std::nullopt /* ex */);
  }
}

template <typename KeyType, typename ValueType>
MergeChannel<KeyType, ValueType>::operator bool() const {
  return processor_;
}

template <typename KeyType, typename ValueType>
template <typename TReceiver>
void MergeChannel<KeyType, ValueType>::addNewReceiver(
    KeyType key, TReceiver receiver) {
  processor_->addNewReceiver(key, std::move(receiver));
}

template <typename KeyType, typename ValueType>
void MergeChannel<KeyType, ValueType>::removeReceiver(KeyType key) {
  processor_->removeReceiver(key);
}

template <typename KeyType, typename ValueType>
folly::F14FastSet<KeyType> MergeChannel<KeyType, ValueType>::getReceiverKeys() {
  return processor_->getReceiverKeys();
}

template <typename KeyType, typename ValueType>
void MergeChannel<KeyType, ValueType>::close(
    std::optional<folly::exception_wrapper> ex) && {
  processor_->destroyHandle(
      ex.has_value() ? detail::CloseResult(std::move(ex.value()))
                     : detail::CloseResult());
  processor_ = nullptr;
}

namespace detail {

template <typename KeyType, typename ValueType>
class IMergeChannelProcessor : public IChannelCallback {
 public:
  virtual void addNewReceiver(KeyType key, Receiver<ValueType> receiver) = 0;

  virtual void removeReceiver(KeyType key) = 0;

  virtual folly::F14FastSet<KeyType> getReceiverKeys() = 0;

  virtual void destroyHandle(CloseResult closeResult) = 0;
};

/**
 * This object does the merging of values from the input receivers to the output
 * receiver. The lifetime of this object is described by the following state
 * machine.
 *
 * The sender and all active receivers can be in one of three conceptual states:
 * Active, CancellationTriggered, or CancellationProcessed (removed). When the
 * sender and all receivers reach the CancellationProcessed state AND the user's
 * MergeChannel object is deleted, this object is deleted.
 *
 * When an input receiver receives a value indicating that the channel has
 * been closed, the state of that receiver transitions from Active directly to
 * CancellationProcessed and the receiver is removed.
 *
 * If the receiver closed with an exception, the state of the sender and all
 * other receivers transitions from Active to CancellationTriggered. In that
 * case, once we receive callbacks indicating the cancellation signal has been
 * received for all other receivers and the sender, the state of the sender and
 * all other receivers transitions to CancellationProcessed (and this object
 * will be deleted once the user destroys their MergeChannel object).
 *
 * When the sender receives notification that the consumer of the output
 * receiver has stopped consuming, the state of the sender transitions from
 * Active directly to CancellationProcessed, and the state of all remaining
 * input receivers transitions from Active to CancellationTriggered. Once we
 * receive callbacks for all input receivers indicating that the cancellation
 * signal has been received, each such receiver is transitioned to the
 * CancellationProcessed state (and this object will be deleted once the user
 * destroys their MergeChannel object).
 *
 * When the user destroys their MergeChannel object, the state of the sender and
 * all remaining receivers transition from Active to CancellationTriggered. This
 * object will then be deleted once the sender and each remaining input receiver
 * transitions to the CancellationProcessed state (after we receive each
 * cancelled callback).
 */
template <typename KeyType, typename ValueType>
class MergeChannelProcessor
    : public IMergeChannelProcessor<KeyType, ValueType> {
 private:
  struct State {
    explicit State(
        ChannelBridgePtr<MergeChannelEvent<KeyType, ValueType>> _sender)
        : sender(std::move(_sender)) {}

    ChannelState getSenderState() {
      return detail::getSenderState(sender.get());
    }

    // The output sender for the merge channel.
    ChannelBridgePtr<MergeChannelEvent<KeyType, ValueType>> sender;

    // A non-owning map from key to receiver.
    folly::F14NodeMap<KeyType, ChannelBridge<ValueType>*> receiversByKey;

    // The set of receivers that feed into this MergeChannel. This map "owns"
    // its receivers. MergeChannelProcessor must free any receiver removed from
    // this map.
    folly::F14NodeMap<ChannelBridge<ValueType>*, const KeyType*> receivers;

    // Whether or not the handle to the MergeChannel has been destroyed.
    bool handleDestroyed{false};
  };

  using WLockedStatePtr = typename folly::Synchronized<State>::WLockedPtr;

 public:
  MergeChannelProcessor(
      Sender<MergeChannelEvent<KeyType, ValueType>> sender,
      folly::Executor::KeepAlive<folly::SequencedExecutor> executor)
      : executor_(std::move(executor)),
        state_(State(std::move(detail::senderGetBridge(sender)))) {
    auto state = state_.wlock();
    CHECK(state->sender->senderWait(this));
  }

  /**
   * Adds a new receiver to be merged, along with a key to allow for later
   * removal.
   */
  void addNewReceiver(KeyType key, Receiver<ValueType> receiver) {
    auto state = state_.wlock();
    if (state->getSenderState() != ChannelState::Active) {
      return;
    }
    auto [unbufferedReceiver, buffer] =
        detail::receiverUnbuffer(std::move(receiver));
    auto existingReceiverIt = state->receiversByKey.find(key);
    if (existingReceiverIt != state->receiversByKey.end()) {
      CHECK(state->receivers.contains(existingReceiverIt->second));
      if (!existingReceiverIt->second->isReceiverCancelled()) {
        // We already have a receiver with the given key. Trigger cancellation
        // on that previous receiver.
        existingReceiverIt->second->receiverCancel();
      }
      auto keyToRemove = existingReceiverIt->first;
      state->receivers[existingReceiverIt->second] = nullptr;
      state->receiversByKey.erase(existingReceiverIt);
      state->sender->senderPush(MergeChannelEvent<KeyType, ValueType>{
          keyToRemove, MergeChannelReceiverRemoved{}});
    }
    auto [it, _] = state->receiversByKey.insert(
        std::make_pair(key, unbufferedReceiver.get()));
    auto* receiverPtr = unbufferedReceiver.get();
    state->receivers.insert(
        std::make_pair(unbufferedReceiver.release(), &it->first));
    state->sender->senderPush(MergeChannelEvent<KeyType, ValueType>{
        key, MergeChannelReceiverAdded{}});
    processAllAvailableValues(state, receiverPtr, std::move(buffer));
  }

  /**
   * Removes the receiver with the given key.
   */
  void removeReceiver(KeyType key) {
    auto state = state_.wlock();
    if (state->getSenderState() != ChannelState::Active) {
      return;
    }
    auto receiverIt = state->receiversByKey.find(key);
    if (receiverIt == state->receiversByKey.end()) {
      return;
    }
    CHECK(state->receivers.contains(receiverIt->second));
    if (!receiverIt->second->isReceiverCancelled()) {
      receiverIt->second->receiverCancel();
    }
    auto keyToRemove = receiverIt->first;
    state->receivers[receiverIt->second] = nullptr;
    state->receiversByKey.erase(receiverIt);
    state->sender->senderPush(MergeChannelEvent<KeyType, ValueType>{
        keyToRemove, MergeChannelReceiverRemoved{}});
  }

  folly::F14FastSet<KeyType> getReceiverKeys() {
    auto state = state_.rlock();
    auto receiverKeys = folly::F14FastSet<KeyType>();
    receiverKeys.reserve(state->receiversByKey.size());
    for (const auto& [key, _] : state->receiversByKey) {
      receiverKeys.insert(key);
    }
    return receiverKeys;
  }

  /**
   * Called when the user's MergeChannel object is destroyed.
   */
  void destroyHandle(CloseResult closeResult) {
    auto state = state_.wlock();
    processHandleDestroyed(state, std::move(closeResult));
  }

  /**
   * Called when one of the channels we are listening to has an update (either
   * a value from an input receiver or a cancellation from the output receiver).
   */
  void consume(ChannelBridgeBase* bridge) override {
    executor_->add([=]() {
      auto state = state_.wlock();
      if (bridge == state->sender.get()) {
        // The consumer of the output receiver has stopped consuming.
        CHECK(state->getSenderState() != ChannelState::CancellationProcessed);
        state->sender->senderClose();
        processSenderCancelled(state);
      } else {
        // One or more values are now available from an input receiver.
        auto* receiver = static_cast<ChannelBridge<ValueType>*>(bridge);
        CHECK(
            getReceiverState(receiver) != ChannelState::CancellationProcessed);
        processAllAvailableValues(state, receiver);
      }
    });
  }

  /**
   * Called after we cancelled one of the channels we were listening to (either
   * the sender or an input receiver).
   */
  void canceled(ChannelBridgeBase* bridge) override {
    executor_->add([=]() {
      auto state = state_.wlock();
      if (bridge == state->sender.get()) {
        // We previously cancelled the sender due to an input receiver closure
        // with an exception (or the closure of all input receivers without an
        // exception). Process the cancellation for the sender.
        CHECK(state->getSenderState() == ChannelState::CancellationTriggered);
        processSenderCancelled(state);
      } else {
        // We previously cancelled this input receiver, either because the
        // consumer of the output receiver stopped consuming or because another
        // input receiver received an exception. Process the cancellation for
        // this input receiver.
        auto* receiver = static_cast<ChannelBridge<ValueType>*>(bridge);
        processReceiverCancelled(state, receiver, CloseResult());
      }
    });
  }

 protected:
  /**
   * Processes all available values from the given input receiver channel
   * (starting from the provided buffer, if present).
   *
   * If an value was received indicating that the input channel has been closed
   * (or if the transform function indicated that channel should be closed), we
   * will process cancellation for the input receiver.
   */
  void processAllAvailableValues(
      WLockedStatePtr& state,
      ChannelBridge<ValueType>* receiver,
      std::optional<ReceiverQueue<ValueType>> buffer = std::nullopt) {
    CHECK(state->receivers.contains(receiver));
    const auto* key = state->receivers.at(receiver);
    auto closeResult = receiver->isReceiverCancelled()
        ? CloseResult()
        : (buffer.has_value()
               ? processValues(state, std::move(buffer.value()), key)
               : std::nullopt);
    while (!closeResult.has_value()) {
      if (receiver->receiverWait(this)) {
        // There are no more values available right now. We will stop processing
        // until the channel fires the consume() callback (indicating that more
        // values are available).
        break;
      }
      auto values = receiver->receiverGetValues();
      CHECK(!values.empty());
      closeResult = processValues(state, std::move(values), key);
    }
    if (closeResult.has_value()) {
      // The receiver received a value indicating channel closure.
      receiver->receiverCancel();
      processReceiverCancelled(state, receiver, std::move(closeResult.value()));
    }
  }

  /**
   * Processes the given set of values for an input receiver. Returns a
   * CloseResult if the given channel was closed, so the caller can stop
   * attempting to process values from it.
   */
  std::optional<CloseResult> processValues(
      WLockedStatePtr& state,
      ReceiverQueue<ValueType> values,
      const KeyType* key) {
    while (!values.empty()) {
      auto inputResult = std::move(values.front());
      values.pop();
      if (inputResult.hasValue()) {
        // We have received a normal value from an input receiver. Write it to
        // the output receiver.
        state->sender->senderPush(MergeChannelEvent<KeyType, ValueType>{
            *key, std::move(inputResult.value())});
      } else {
        // The input receiver was closed.
        return inputResult.hasException()
            ? CloseResult(std::move(inputResult.exception()))
            : CloseResult();
      }
    }
    return std::nullopt;
  }

  /**
   * Processes the cancellation of an input receiver.
   */
  void processReceiverCancelled(
      WLockedStatePtr& state,
      ChannelBridge<ValueType>* receiver,
      CloseResult closeResult) {
    CHECK(getReceiverState(receiver) == ChannelState::CancellationTriggered);
    auto* key = state->receivers.at(receiver);
    if (key != nullptr) {
      auto keyToRemove = *key;
      CHECK_EQ(state->receiversByKey.erase(keyToRemove), 1);
      if (state->getSenderState() == ChannelState::Active) {
        state->sender->senderPush(MergeChannelEvent<KeyType, ValueType>{
            keyToRemove,
            MergeChannelReceiverClosed{
                closeResult.exception.has_value()
                    ? std::move(closeResult.exception.value())
                    : folly::exception_wrapper()}});
      }
    }
    state->receivers.erase(receiver);
    (ChannelBridgePtr<ValueType>(receiver));
    maybeDelete(state);
  }

  /**
   * Processes the cancellation of the sender (indicating that the consumer of
   * the output receiver has stopped consuming). We will trigger cancellation
   * for all input receivers not already cancelled.
   */
  void processSenderCancelled(WLockedStatePtr& state) {
    CHECK(state->getSenderState() == ChannelState::CancellationTriggered);
    state->sender = nullptr;
    for (auto [receiver, _] : state->receivers) {
      if (getReceiverState(receiver) == ChannelState::Active) {
        receiver->receiverCancel();
      }
    }
    maybeDelete(state);
  }

  /**
   * Processes the destruction of the user's MergeChannel object.  We will
   * close the sender and trigger cancellation for all input receivers not
   * already cancelled.
   */
  void processHandleDestroyed(WLockedStatePtr& state, CloseResult closeResult) {
    CHECK(!state->handleDestroyed);
    state->handleDestroyed = true;
    if (state->getSenderState() == ChannelState::Active) {
      for (auto [key, receiver] : state->receiversByKey) {
        state->receivers[receiver] = nullptr;
        state->sender->senderPush(MergeChannelEvent<KeyType, ValueType>{
            key, MergeChannelReceiverRemoved{}});
      }
      if (closeResult.exception.has_value()) {
        state->sender->senderClose(std::move(closeResult.exception.value()));
      } else {
        state->sender->senderClose();
      }
    }
    for (auto [receiver, _] : state->receivers) {
      if (getReceiverState(receiver) == ChannelState::Active) {
        receiver->receiverCancel();
      }
    }
    maybeDelete(state);
  }

  /**
   * Deletes this object if we have already processed cancellation for the
   * sender and all input receivers, and if the user's MergeChannel object was
   * destroyed.
   */
  void maybeDelete(WLockedStatePtr& state) {
    if (state->getSenderState() == ChannelState::CancellationProcessed &&
        state->receivers.empty() && state->handleDestroyed) {
      state.unlock();
      delete this;
    }
  }

  ChannelState getReceiverState(ChannelBridge<ValueType>* receiver) {
    return detail::getReceiverState(receiver);
  }

  folly::Executor::KeepAlive<folly::SequencedExecutor> executor_;
  folly::Synchronized<State> state_;
};
} // namespace detail

template <typename KeyType, typename ValueType>
std::pair<
    Receiver<MergeChannelEvent<KeyType, ValueType>>,
    MergeChannel<KeyType, ValueType>>
createMergeChannel(
    folly::Executor::KeepAlive<folly::SequencedExecutor> executor) {
  auto [receiver, sender] =
      Channel<MergeChannelEvent<KeyType, ValueType>>::create();
  auto* processor = new detail::MergeChannelProcessor<KeyType, ValueType>(
      std::move(sender), std::move(executor));
  return std::make_pair(
      std::move(receiver), MergeChannel<KeyType, ValueType>(processor));
}
} // namespace channels
} // namespace folly
