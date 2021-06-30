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

#pragma once

#include <fmt/format.h>
#include <folly/CancellationToken.h>
#include <folly/experimental/channels/Channel.h>
#include <folly/experimental/channels/ConsumeChannel.h>
#include <folly/experimental/channels/Producer.h>
#include <folly/experimental/coro/Task.h>

namespace folly {
namespace channels {

template <typename TValue>
Producer<TValue>::KeepAlive::KeepAlive(Producer<TValue>* ptr) : ptr_(ptr) {}

template <typename TValue>
Producer<TValue>::KeepAlive::~KeepAlive() {
  if (ptr_ && --ptr_->refCount_ == 0) {
    auto deleteTask =
        folly::coro::co_invoke([ptr = ptr_]() -> folly::coro::Task<void> {
          delete ptr;
          co_return;
        });
    std::move(deleteTask).scheduleOn(ptr_->getExecutor()).start();
  }
}

template <typename TValue>
Producer<TValue>::KeepAlive::KeepAlive(
    Producer<TValue>::KeepAlive&& other) noexcept
    : ptr_(std::exchange(other.ptr_, nullptr)) {}

template <typename TValue>
typename Producer<TValue>::KeepAlive& Producer<TValue>::KeepAlive::operator=(
    Producer<TValue>::KeepAlive&& other) noexcept {
  if (&other == this) {
    return *this;
  }
  ptr_ = std::exchange(other.ptr_, nullptr);
  return *this;
}

template <typename TValue>
Producer<TValue>::Producer(
    Sender<TValue> sender,
    folly::Executor::KeepAlive<folly::SequencedExecutor> executor)
    : sender_(std::move(detail::senderGetBridge(sender))),
      executor_(std::move(executor)) {
  CHECK(sender_->senderWait(this));
}

template <typename TValue>
void Producer<TValue>::write(TValue value) {
  executor_->add([this, value = std::move(value)]() mutable {
    sender_->senderPush(std::move(value));
  });
}

template <typename TValue>
void Producer<TValue>::close(std::optional<folly::exception_wrapper> ex) {
  executor_->add([this, ex = std::move(ex)]() mutable {
    if (ex.has_value()) {
      sender_->senderClose(std::move(ex.value()));
    } else {
      sender_->senderClose();
    }
  });
}

template <typename TValue>
bool Producer<TValue>::isClosed() {
  return sender_->isSenderClosed();
}

template <typename TValue>
folly::Executor::KeepAlive<folly::SequencedExecutor>
Producer<TValue>::getExecutor() {
  return executor_;
}

template <typename TValue>
typename Producer<TValue>::KeepAlive Producer<TValue>::getKeepAlive() {
  refCount_.fetch_add(1, std::memory_order_relaxed);
  return KeepAlive(this);
}

template <typename TValue>
void Producer<TValue>::consume(detail::ChannelBridgeBase*) {
  onClosed().scheduleOn(getExecutor()).start([=](auto) {
    // Decrement ref count
    KeepAlive(this);
  });
}

template <typename TValue>
void Producer<TValue>::canceled(detail::ChannelBridgeBase* bridge) {
  consume(bridge);
}

namespace detail {
template <typename TProducer>
class ProducerImpl : public TProducer {
  template <typename ProducerType, typename... Args>
  friend Receiver<typename ProducerType::ValueType> makeProducer(
      folly::Executor::KeepAlive<folly::SequencedExecutor> executor,
      Args&&... args);

 public:
  using TProducer::TProducer;

 private:
  void ensureMakeProducerUsedForCreation() override {}
};
} // namespace detail

template <typename TProducer, typename... Args>
Receiver<typename TProducer::ValueType> makeProducer(
    folly::Executor::KeepAlive<folly::SequencedExecutor> executor,
    Args&&... args) {
  using TValue = typename TProducer::ValueType;
  auto [receiver, sender] = Channel<TValue>::create();
  new detail::ProducerImpl<TProducer>(
      std::move(sender), std::move(executor), std::forward<Args>(args)...);
  return std::move(receiver);
}
} // namespace channels
} // namespace folly
