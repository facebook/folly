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

namespace folly {
namespace channels {

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
  if (!anySubscribers()) {
    // There are currently no output receivers. Store the new output receiver.
    senders_.set(detail::senderGetBridge(newSender).release());
  } else if (!hasSenderSet()) {
    // There is currently exactly one output receiver. Convert to a sender set.
    auto senderSet = std::make_unique<
        folly::F14FastSet<detail::ChannelBridgePtr<ValueType>>>();
    senderSet->insert(detail::ChannelBridgePtr<ValueType>(getSingleSender()));
    senderSet->insert(std::move(detail::senderGetBridge(newSender)));
    senders_.set(senderSet.release());
  } else {
    // There are currently more than one output receivers. Add the new receiver
    // to the existing sender set.
    auto* senderSet = getSenderSet();
    senderSet->insert(std::move(detail::senderGetBridge(newSender)));
  }
}

template <typename ValueType>
bool FanoutSender<ValueType>::anySubscribers() {
  clearSendersWithClosedReceivers();
  return hasSenderSet() || getSingleSender() != nullptr;
}

template <typename ValueType>
std::uint64_t FanoutSender<ValueType>::numSubscribers() const {
  if (senders_.index() == 0) {
    auto sender =
        senders_.get(folly::tag_t<detail::ChannelBridge<ValueType>>{});
    return sender ? 1 : 0;
  } else if (senders_.index() == 1) {
    auto senders = senders_.get(
        folly::tag_t<folly::F14FastSet<detail::ChannelBridgePtr<ValueType>>>{});
    return senders ? senders->size() : 0;
  } else {
    return 0;
  }
}

template <typename ValueType>
template <typename U>
void FanoutSender<ValueType>::write(U&& element) {
  clearSendersWithClosedReceivers();
  if (!anySubscribers()) {
    // There are currently no output receivers to write to.
    return;
  } else if (!hasSenderSet()) {
    // There is exactly one output receiver. Write the value to that receiver.
    getSingleSender()->senderPush(std::forward<U>(element));
  } else {
    // There are more than one output receivers. Write the value to all
    // receivers.
    for (auto& senderBridge : *getSenderSet()) {
      senderBridge->senderPush(element);
    }
  }
}

template <typename ValueType>
void FanoutSender<ValueType>::close(folly::exception_wrapper ex) && {
  clearSendersWithClosedReceivers();
  if (!anySubscribers()) {
    // There are no output receivers to close.
    return;
  } else if (!hasSenderSet()) {
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
    // There are more than one output receivers to close.
    auto senderSet =
        std::unique_ptr<F14FastSet<detail::ChannelBridgePtr<ValueType>>>(
            getSenderSet());
    senders_.set(static_cast<detail::ChannelBridge<ValueType>*>(nullptr));
    for (auto& senderBridge : *senderSet) {
      if (ex) {
        senderBridge->senderClose(ex);
      } else {
        senderBridge->senderClose();
      }
    }
  }
}

template <typename ValueType>
bool FanoutSender<ValueType>::hasSenderSet() {
  return senders_.index() == 1;
}

template <typename ValueType>
detail::ChannelBridge<ValueType>* FanoutSender<ValueType>::getSingleSender() {
  return senders_.get(folly::tag_t<detail::ChannelBridge<ValueType>>{});
}

template <typename ValueType>
F14FastSet<detail::ChannelBridgePtr<ValueType>>*
FanoutSender<ValueType>::getSenderSet() {
  return senders_.get(
      folly::tag_t<folly::F14FastSet<detail::ChannelBridgePtr<ValueType>>>{});
}

template <typename ValueType>
void FanoutSender<ValueType>::clearSendersWithClosedReceivers() {
  if (hasSenderSet()) {
    // There are currently more than one output receivers. Check all of them to
    // see if any have been cancelled.
    auto* senderSet = getSenderSet();
    for (auto it = senderSet->begin(); it != senderSet->end();) {
      auto values = it->get()->senderGetValues();
      if (!values.empty()) {
        it->get()->senderClose();
        it = senderSet->erase(it);
      } else {
        ++it;
      }
    }
    if (senderSet->empty()) {
      // After erasing all cancelled output receivers, there are no remaining
      // receivers.
      (std::unique_ptr<F14FastSet<detail::ChannelBridgePtr<ValueType>>>(
          senderSet));
      senders_.set(static_cast<detail::ChannelBridge<ValueType>*>(nullptr));
      senderSet = nullptr;
    } else if (senderSet->size() == 1) {
      // After erasing all cancelled output receivers, there is exactly one
      // remaining receiver. Move it out of the set, and store it as a single
      // receiver.
      auto senderSetUniqPtr =
          std::unique_ptr<F14FastSet<detail::ChannelBridgePtr<ValueType>>>(
              senderSet);
      senderSetUniqPtr->eraseInto(senderSet->begin(), [&](auto&& senderBridge) {
        senders_.set(senderBridge.release());
      });
      senderSet = nullptr;
    }
  } else {
    auto* bridge = getSingleSender();
    if (bridge != nullptr) {
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
