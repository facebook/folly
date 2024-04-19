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

#include <folly/container/F14Set.h>
#include <folly/executors/SequencedExecutor.h>
#include <folly/experimental/channels/Channel.h>
#include <folly/experimental/channels/detail/Utility.h>

namespace folly {
namespace channels {

namespace detail {
template <typename ValueType>
class FanoutSenderProcessor : public IChannelCallback {
 private:
  struct State {
    folly::F14FastSet<ChannelBridge<ValueType>*> senders_;
    bool handleDestroyed_{false};
  };

  using WLockedStatePtr = typename folly::Synchronized<State>::WLockedPtr;

 public:
  /**
   * Subscribes with an already-created sender.
   */
  void addSender(detail::ChannelBridgePtr<ValueType> sender) {
    auto state = state_.wlock();
    sender->senderWait(this);
    state->senders_.insert(sender.release());
  }

  /**
   * Sends the given value to all corresponding receivers.
   */
  template <typename U = ValueType>
  void write(U&& element) {
    auto state = state_.wlock();
    for (auto* sender : state->senders_) {
      sender->senderPush(element);
    }
  }

  /**
   * This is called when the user's FanoutSender object has been destroyed.
   */
  void destroyHandle(CloseResult closeResult) {
    processHandleDestroyed(state_.wlock(), std::move(closeResult));
  }

  /**
   * Returns whether this fanout channel has any output receivers.
   */
  size_t numSubscribers() const { return state_.rlock()->senders_.size(); }

  std::pair<bool, ChannelBridgePtr<ValueType>>
  stealSenderAndDestorySelfIfSingle() {
    auto state = state_.wlock();
    if (state->senders_.empty()) {
      // There are no remaining senders. We will destroy ourselves.
      state->handleDestroyed_ = true;
      maybeDelete(std::move(state));
      return std::make_pair(true, ChannelBridgePtr<ValueType>());
    } else if (state->senders_.size() == 1) {
      // There is one remaining sender.
      auto* sender = *state->senders_.begin();
      auto* callback = sender->cancelSenderWait();
      if (callback) {
        // We successfully cancelled the callback, so we can now destroy
        // ourselves and return the sender.
        state->senders_.clear();
        state->handleDestroyed_ = true;
        maybeDelete(std::move(state));
        return std::make_pair(true, ChannelBridgePtr<ValueType>(sender));
      } else {
        // We failed to cancel the callback. This means that another thread is
        // invoking the callback by calling consume(), letting us know that the
        // corresponding receiver was deleted. The callback will start running
        // once we release the lock, so we will let the callback delete the
        // sender (and then destroy ourselves).
        return std::make_pair(true, ChannelBridgePtr<ValueType>());
      }
    } else {
      // There is more than one sender. Do not destroy ourselves.
      return std::make_pair(false, ChannelBridgePtr<ValueType>());
    }
  }

  static ChannelState getSenderState(ChannelBridge<ValueType>* sender) {
    return detail::getSenderState(sender);
  }

 private:
  /**
   * Called when receiving a cancellation from an output receiver.
   */
  void consume(ChannelBridgeBase* bridge) override {
    // The consumer of an output receiver has stopped consuming.
    auto state = state_.wlock();
    auto* sender = static_cast<ChannelBridge<ValueType>*>(bridge);
    CHECK_NE(getSenderState(sender), ChannelState::CancellationProcessed);
    sender->senderClose();
    processSenderCancelled(std::move(state), sender);
  }

  void canceled(ChannelBridgeBase*) override {
    // We cancel the callback before we close the sender explicitly, so this
    // should never be hit.
    CHECK(false);
  }

  /**
   * Processes the cancellation of a sender (indicating that the consumer of
   * the corresponding output receiver has stopped consuming).
   */
  void processSenderCancelled(
      WLockedStatePtr state, ChannelBridge<ValueType>* sender) {
    CHECK_EQ(getSenderState(sender), ChannelState::CancellationTriggered);
    state->senders_.erase(sender);
    deleteSender(sender);
    maybeDelete(std::move(state));
  }

  /**
   * Processes the destruction of the user's FanoutChannel object.  We will
   * cancel the receiver and trigger cancellation for all senders not already
   * cancelled.
   */
  void processHandleDestroyed(WLockedStatePtr state, CloseResult closeResult) {
    CHECK(!state->handleDestroyed_);
    state->handleDestroyed_ = true;
    auto senders = state->senders_;
    for (auto* sender : senders) {
      auto* callback = sender->cancelSenderWait();
      if (closeResult.exception.has_value()) {
        sender->senderClose(closeResult.exception.value());
      } else {
        sender->senderClose();
      }
      if (callback) {
        // We successfully cancelled the callback, so we can now delete the
        // sender.
        CHECK_EQ(callback, this);
        state->senders_.erase(sender);
        deleteSender(sender);
      } else {
        // We failed to cancel the callback. This means that another thread is
        // invoking the callback by calling consume(), letting us know that the
        // corresponding receiver was deleted. The callback will start running
        // once we release the lock, so we will let the callback delete the
        // sender.
      }
    }
    maybeDelete(std::move(state));
  }

  /*
   * Deletes the given sender.
   */
  void deleteSender(ChannelBridge<ValueType>* sender) {
    (ChannelBridgePtr<ValueType>(sender));
  }

  /**
   * Deletes this object if we have already processed cancellation for the
   * receiver and all senders, and if the user's FanoutChannel object was
   * destroyed.
   */
  void maybeDelete(WLockedStatePtr state) {
    if (state->senders_.empty() && state->handleDestroyed_) {
      state.unlock();
      delete this;
    }
  }

  folly::Synchronized<State> state_;
};
} // namespace detail

template <typename ValueType>
FanoutSender<ValueType>::FanoutSender()
    : senders_(static_cast<detail::ChannelBridge<ValueType>*>(nullptr)) {}

template <typename ValueType>
FanoutSender<ValueType>::FanoutSender(FanoutSender&& other) noexcept
    : senders_(std::move(other.senders_)) {}

template <typename ValueType>
FanoutSender<ValueType>& FanoutSender<ValueType>::operator=(
    FanoutSender&& other) noexcept {
  if (&other == this) {
    return *this;
  }
  std::move(*this).close();
  senders_ = std::move(senders_);
  return *this;
}

template <typename ValueType>
FanoutSender<ValueType>::~FanoutSender() {
  std::move(*this).close();
}

template <typename ValueType>
Receiver<ValueType> FanoutSender<ValueType>::subscribe(
    std::vector<ValueType> initialValues) {
  auto [newReceiver, newSender] = Channel<ValueType>::create();
  for (auto&& initialValue : initialValues) {
    newSender.write(std::move(initialValue));
  }
  subscribe(std::move(newSender));
  return std::move(newReceiver);
}

template <typename ValueType>
void FanoutSender<ValueType>::subscribe(Sender<ValueType> newSender) {
  clearSendersWithClosedReceivers();
  if (!anySubscribersImpl()) {
    // There are currently no output receivers. Store the new output receiver.
    senders_.set(detail::senderGetBridge(newSender).release());
  } else if (!hasProcessor()) {
    // There is currently exactly one output receiver. Convert to a processor.
    auto* processor = new detail::FanoutSenderProcessor<ValueType>();
    processor->addSender(
        detail::ChannelBridgePtr<ValueType>(getSingleSender()));
    processor->addSender(std::move(detail::senderGetBridge(newSender)));
    senders_.set(processor);
  } else {
    // There are currently more than one output receivers. Add the new receiver
    // to the existing processor.
    auto* processor = getProcessor();
    processor->addSender(std::move(detail::senderGetBridge(newSender)));
  }
}

template <typename ValueType>
bool FanoutSender<ValueType>::anySubscribers() const {
  clearSendersWithClosedReceivers();
  return anySubscribersImpl();
}

template <typename ValueType>
std::uint64_t FanoutSender<ValueType>::numSubscribers() const {
  clearSendersWithClosedReceivers();
  if (!anySubscribersImpl()) {
    return 0;
  } else if (!hasProcessor()) {
    return 1;
  } else {
    return getProcessor()->numSubscribers();
  }
}

template <typename ValueType>
template <typename U>
void FanoutSender<ValueType>::write(U&& element) {
  clearSendersWithClosedReceivers();
  if (!anySubscribersImpl()) {
    // There are currently no output receivers to write to.
    return;
  } else if (!hasProcessor()) {
    // There is exactly one output receiver. Write the value to that receiver.
    getSingleSender()->senderPush(std::forward<U>(element));
  } else {
    getProcessor()->write(std::forward<U>(element));
  }
}

template <typename ValueType>
void FanoutSender<ValueType>::close(exception_wrapper ex) && {
  clearSendersWithClosedReceivers();
  if (!anySubscribersImpl()) {
    // There are no output receivers to close.
    return;
  } else if (!hasProcessor()) {
    // There is exactly one output receiver to close.
    if (ex) {
      getSingleSender()->senderClose(ex);
    } else {
      getSingleSender()->senderClose();
    }
    // Delete the output receiver.
    (detail::ChannelBridgePtr<ValueType>(getSingleSender()));
    senders_.set(static_cast<detail::ChannelBridge<ValueType>*>(nullptr));
  } else {
    // There is more than one output receiver to close.
    getProcessor()->destroyHandle(
        ex ? detail::CloseResult(std::move(ex)) : detail::CloseResult());
    senders_.set(static_cast<detail::ChannelBridge<ValueType>*>(nullptr));
  }
}

template <typename ValueType>
bool FanoutSender<ValueType>::anySubscribersImpl() const {
  return hasProcessor() || getSingleSender() != nullptr;
}

template <typename ValueType>
bool FanoutSender<ValueType>::hasProcessor() const {
  return senders_.index() == 1;
}

template <typename ValueType>
detail::ChannelBridge<ValueType>* FanoutSender<ValueType>::getSingleSender()
    const {
  return senders_.get(folly::tag_t<detail::ChannelBridge<ValueType>>{});
}

template <typename ValueType>
detail::FanoutSenderProcessor<ValueType>*
FanoutSender<ValueType>::getProcessor() const {
  return senders_.get(folly::tag_t<detail::FanoutSenderProcessor<ValueType>>{});
}

template <typename ValueType>
void FanoutSender<ValueType>::clearSendersWithClosedReceivers() const {
  if (hasProcessor()) {
    auto [processorDestroyed, remainingSender] =
        getProcessor()->stealSenderAndDestorySelfIfSingle();
    if (processorDestroyed) {
      if (remainingSender) {
        senders_.set(remainingSender.release());
      } else {
        senders_.set(static_cast<detail::ChannelBridge<ValueType>*>(nullptr));
      }
    }
  } else {
    auto* bridge = getSingleSender();
    if (bridge) {
      // There is currently exactly one output receiver. Check to see if it has
      // been cancelled.
      auto values = bridge->senderGetValues();
      if (!values.empty()) {
        bridge->senderClose();
        senders_.set(static_cast<detail::ChannelBridge<ValueType>*>(nullptr));
        // Delete the output receiver.
        (detail::ChannelBridgePtr<ValueType>(bridge));
      }
    }
  }
}
} // namespace channels
} // namespace folly
