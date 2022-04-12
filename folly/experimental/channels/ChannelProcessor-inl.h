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

#include <folly/executors/SerialExecutor.h>
#include <folly/experimental/channels/ChannelProcessor.h>
#include <folly/experimental/channels/ConsumeChannel.h>
#include <folly/experimental/channels/MergeChannel.h>
#include <folly/experimental/channels/Transform.h>
#include <folly/experimental/channels/detail/IntrusivePtr.h>

namespace folly {
namespace channels {
namespace detail {

template <typename KeyType>
class ChannelProcessorImpl {
 public:
  ChannelProcessorImpl(
      std::vector<folly::Executor::KeepAlive<folly::SequencedExecutor>>
          executors,
      std::shared_ptr<folly::channels::RateLimiter> rateLimiter,
      MergeChannel<KeyType, folly::Unit> mergeChannel,
      Receiver<MergeChannelEvent<KeyType, folly::Unit>> mergeChannelReceiver)
      : implState_(make_intrusive<ImplState>(
            std::move(executors), std::move(rateLimiter))),
        channels_(std::move(mergeChannel)),
        handle_(consumeChannelWithCallback(
            std::move(mergeChannelReceiver),
            implState_->executors[0],
            [](folly::Try<MergeChannelEvent<KeyType, folly::Unit>>)
                -> folly::coro::Task<bool> {
              // Do nothing
              co_return true;
            })) {}

  template <typename ReceiverType, typename OnUpdateFunc>
  void addChannel(KeyType key, ReceiverType receiver, OnUpdateFunc onUpdate) {
    using InputValueType = typename ReceiverType::ValueType;
    channels_.removeReceiver(key);
    channels_.addNewReceiver(
        std::move(key),
        transform(
            std::move(receiver),
            Transformer<InputValueType, OnUpdateFunc>(
                implState_, std::move(onUpdate))));
  }

  template <
      typename InitializeArg,
      typename InitializeFunc,
      typename OnUpdateFunc>
  void addResumableChannelWithState(
      KeyType key,
      InitializeArg initializeArg,
      InitializeFunc initialize,
      OnUpdateFunc onUpdate) {
    addResumableChannelWithState(
        std::move(key),
        std::move(initializeArg),
        std::move(initialize),
        std::move(onUpdate),
        NoChannelState());
  }

  template <
      typename InitializeArg,
      typename InitializeFunc,
      typename OnUpdateFunc,
      typename ChannelState>
  void addResumableChannelWithState(
      KeyType key,
      InitializeArg initializeArg,
      InitializeFunc initialize,
      OnUpdateFunc onUpdate,
      ChannelState channelState) {
    using ReceiverType = typename decltype(initialize(
        std::move(initializeArg), channelState))::StorageType;
    using InputValueType = typename ReceiverType::ValueType;
    channels_.removeReceiver(key);
    channels_.addNewReceiver(
        std::move(key),
        resumableTransform(
            std::move(initializeArg),
            ResumableTransformer<
                InitializeArg,
                InputValueType,
                InitializeFunc,
                OnUpdateFunc,
                ChannelState>(
                implState_,
                std::move(initialize),
                std::move(onUpdate),
                std::move(channelState))));
  }

  void removeChannel(const KeyType& keyType) {
    channels_.removeReceiver(keyType);
  }

 private:
  struct NoChannelState {};

  template <
      typename Function,
      typename ReturnType =
          typename std::invoke_result_t<Function>::StorageType>
  static folly::coro::Task<ReturnType> catchNonCoroException(Function func) {
    auto result = folly::makeTryWith(std::move(func));
    if (result.hasException()) {
      return folly::coro::makeErrorTask<ReturnType>(
          std::move(result.exception()));
    } else {
      return std::move(result.value());
    }
  }

  struct ImplState : public IntrusivePtrBase<ImplState> {
    ImplState(
        std::vector<folly::Executor::KeepAlive<folly::SequencedExecutor>>
            _executors,
        std::shared_ptr<folly::channels::RateLimiter> _rateLimiter)
        : executors(std::move(_executors)),
          rateLimiter(std::move(_rateLimiter)) {}

    std::vector<folly::Executor::KeepAlive<folly::SequencedExecutor>> executors;
    std::shared_ptr<folly::channels::RateLimiter> rateLimiter;
  };

  template <typename InputValueType, typename OnUpdateFunc>
  class Transformer : public std::tuple<OnUpdateFunc> {
   public:
    Transformer(intrusive_ptr<ImplState> implState, OnUpdateFunc onUpdate)
        : std::tuple<OnUpdateFunc>(std::move(onUpdate)),
          implState_(std::move(implState)) {}

    folly::Executor::KeepAlive<folly::SequencedExecutor> getExecutor() {
      return implState_->executors
          [std::hash<decltype(this)>()(this) % implState_->executors.size()];
    }

    std::shared_ptr<folly::channels::RateLimiter> getRateLimiter() {
      return implState_->rateLimiter;
    }

    folly::coro::AsyncGenerator<folly::Unit&&> transformValue(
        folly::Try<InputValueType> value) {
      auto result = co_await folly::coro::co_awaitTry(catchNonCoroException(
          [&] { return std::get<OnUpdateFunc>(*this)(std::move(value)); }));
      if (result.template hasException<folly::OperationCancelled>() ||
          result.template hasException<OnClosedException>()) {
        co_yield folly::coro::co_error(OnClosedException());
      } else if (result.hasException()) {
        LOG(FATAL) << fmt::format(
            "Encountered exception from callback when consuming channel of "
            "type {}: {}",
            typeid(InputValueType).name(),
            result.exception().what());
      }
    }

   private:
    intrusive_ptr<ImplState> implState_;
  };

  template <
      typename InitializeArg,
      typename InputValueType,
      typename InitializeFunc,
      typename OnUpdateFunc,
      typename ChannelState>
  class ResumableTransformer
      : public std::tuple<InitializeFunc, OnUpdateFunc, ChannelState> {
   public:
    ResumableTransformer(
        intrusive_ptr<ImplState> implState,
        InitializeFunc initialize,
        OnUpdateFunc onUpdate,
        ChannelState channelState)
        : std::tuple<InitializeFunc, OnUpdateFunc, ChannelState>(
              std::move(initialize),
              std::move(onUpdate),
              std::move(channelState)),
          implState_(std::move(implState)) {}

    folly::Executor::KeepAlive<folly::SequencedExecutor> getExecutor() {
      return implState_->executors
          [std::hash<decltype(this)>()(this) % implState_->executors.size()];
    }

    std::shared_ptr<folly::channels::RateLimiter> getRateLimiter() {
      return implState_->rateLimiter;
    }

    folly::coro::Task<
        std::pair<std::vector<folly::Unit>, Receiver<InputValueType>>>
    initializeTransform(InitializeArg initializeArg) {
      auto result = co_await folly::coro::co_awaitTry(
          initialize(std::move(initializeArg)));
      if (result.template hasException<folly::OperationCancelled>() ||
          result.template hasException<OnClosedException>()) {
        co_yield folly::coro::co_error(OnClosedException());
      } else if (result.hasException()) {
        LOG(FATAL) << folly::sformat(
            "Encountered exception from callback when consuming channel of "
            "type {}: {}",
            typeid(InputValueType).name(),
            result.exception().what());
      }
      co_return std::make_pair(
          std::vector<folly::Unit>(), std::move(result.value()));
    }

    folly::coro::AsyncGenerator<folly::Unit&&> transformValue(
        folly::Try<InputValueType> value) {
      auto result =
          co_await folly::coro::co_awaitTry(onUpdate(std::move(value)));
      if (result
              .template hasException<ReinitializeException<InitializeArg>>()) {
        co_yield folly::coro::co_error(std::move(result.exception()));
      } else if (
          result.template hasException<folly::OperationCancelled>() ||
          result.template hasException<OnClosedException>()) {
        co_yield folly::coro::co_error(OnClosedException());
      } else if (result.hasException()) {
        LOG(FATAL) << folly::sformat(
            "Encountered exception from callback when consuming channel of "
            "type {}: {}",
            typeid(InputValueType).name(),
            result.exception().what());
      }
    }

   private:
    folly::coro::Task<Receiver<InputValueType>> initialize(
        InitializeArg initializeArg) {
      if constexpr (std::is_same_v<ChannelState, NoChannelState>) {
        co_return co_await catchNonCoroException([&] {
          return std::get<InitializeFunc>(*this)(std::move(initializeArg));
        });
      } else {
        co_return co_await catchNonCoroException([&] {
          return std::get<InitializeFunc>(*this)(
              std::move(initializeArg), std::get<ChannelState>(*this));
        });
      }
    }

    folly::coro::Task<void> onUpdate(folly::Try<InputValueType> value) {
      if constexpr (std::is_same_v<ChannelState, NoChannelState>) {
        co_await catchNonCoroException(
            [&] { return std::get<OnUpdateFunc>(*this)(std::move(value)); });
      } else {
        co_await catchNonCoroException([&] {
          return std::get<OnUpdateFunc>(*this)(
              std::move(value), std::get<ChannelState>(*this));
        });
      }
    }

    intrusive_ptr<ImplState> implState_;
  };

  intrusive_ptr<ImplState> implState_;
  MergeChannel<KeyType, folly::Unit> channels_;
  ChannelCallbackHandle handle_;
};
} // namespace detail

template <typename KeyType>
ChannelProcessor<KeyType>::ChannelProcessor(
    std::unique_ptr<detail::ChannelProcessorImpl<KeyType>> impl)
    : impl_(std::move(impl)) {}

template <typename KeyType>
template <typename ReceiverType, typename OnUpdateFunc>
void ChannelProcessor<KeyType>::addChannel(
    KeyType key, ReceiverType receiver, OnUpdateFunc onUpdate) {
  impl_->addChannel(std::move(key), std::move(receiver), std::move(onUpdate));
}

template <typename KeyType>
template <
    typename InitializeArg,
    typename InitializeFunc,
    typename OnUpdateFunc>
void ChannelProcessor<KeyType>::addResumableChannel(
    KeyType key,
    InitializeArg initializeArg,
    InitializeFunc initialize,
    OnUpdateFunc onUpdate) {
  impl_->addResumableChannel(
      std::move(key),
      std::move(initializeArg),
      std::move(initialize),
      std::move(onUpdate));
}

template <typename KeyType>
template <
    typename InitializeArg,
    typename InitializeFunc,
    typename OnUpdateFunc,
    typename ChannelState>
void ChannelProcessor<KeyType>::addResumableChannelWithState(
    KeyType key,
    InitializeArg initializeArg,
    InitializeFunc initialize,
    OnUpdateFunc onUpdate,
    ChannelState channelState) {
  impl_->addResumableChannelWithState(
      std::move(key),
      std::move(initializeArg),
      std::move(initialize),
      std::move(onUpdate),
      std::move(channelState));
}

template <typename KeyType>
void ChannelProcessor<KeyType>::removeChannel(const KeyType& keyType) {
  impl_->removeChannel(keyType);
}

template <typename KeyType>
void ChannelProcessor<KeyType>::close() && {
  impl_.reset();
}

template <typename KeyType>
ChannelProcessor<KeyType> createChannelProcessor(
    folly::Executor::KeepAlive<> executor,
    std::shared_ptr<RateLimiter> rateLimiter,
    size_t numSequencedExecutors) {
  CHECK_GT(numSequencedExecutors, 0);
  auto executors =
      std::vector<folly::Executor::KeepAlive<folly::SequencedExecutor>>();
  for (size_t i = 0; i < numSequencedExecutors; i++) {
    executors.push_back(folly::SerialExecutor::create(executor));
  }
  auto [mergeChannelReceiver, mergeChannel] =
      createMergeChannel<KeyType, folly::Unit>(executors[0]);
  return ChannelProcessor<KeyType>(
      std::make_unique<detail::ChannelProcessorImpl<KeyType>>(
          std::move(executors),
          std::move(rateLimiter),
          std::move(mergeChannel),
          std::move(mergeChannelReceiver)));
}
} // namespace channels
} // namespace folly
