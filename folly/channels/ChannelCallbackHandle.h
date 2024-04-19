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

#include <folly/IntrusiveList.h>
#include <folly/ScopeGuard.h>
#include <folly/experimental/channels/Channel.h>

namespace folly {
namespace channels {

namespace detail {
class ChannelCallbackProcessor : public IChannelCallback {
 public:
  virtual void onHandleDestroyed() = 0;
};
} // namespace detail

/**
 * A callback handle for a consumption operation on a channel. The consumption
 * operation will be cancelled when this handle is destroyed.
 */
class ChannelCallbackHandle {
 public:
  ChannelCallbackHandle() : processor_(nullptr) {}

  explicit ChannelCallbackHandle(detail::ChannelCallbackProcessor* processor)
      : processor_(processor) {}

  ~ChannelCallbackHandle() {
    if (processor_) {
      processor_->onHandleDestroyed();
    }
  }

  ChannelCallbackHandle(ChannelCallbackHandle&& other) noexcept
      : processor_(std::exchange(other.processor_, nullptr)) {}

  ChannelCallbackHandle& operator=(ChannelCallbackHandle&& other) {
    if (&other == this) {
      return *this;
    }
    reset();
    processor_ = std::exchange(other.processor_, nullptr);
    return *this;
  }

  void reset() {
    if (processor_) {
      processor_->onHandleDestroyed();
      processor_ = nullptr;
    }
  }

 private:
  detail::ChannelCallbackProcessor* processor_;
};

namespace detail {

/**
 * A wrapper around a ChannelCallbackHandle that belongs to an intrusive linked
 * list. When the holder is destroyed, the object will automatically be unlinked
 * from the linked list that it is in (if any).
 */
struct ChannelCallbackHandleHolder {
  explicit ChannelCallbackHandleHolder(ChannelCallbackHandle _handle)
      : handle(std::move(_handle)) {}

  ChannelCallbackHandleHolder(ChannelCallbackHandleHolder&& other) noexcept
      : handle(std::move(other.handle)) {
    hook.swap_nodes(other.hook);
  }

  ChannelCallbackHandleHolder& operator=(
      ChannelCallbackHandleHolder&& other) noexcept {
    if (&other == this) {
      return *this;
    }
    handle = std::move(other.handle);
    hook.unlink();
    hook.swap_nodes(other.hook);
    return *this;
  }

  void requestCancellation() { handle.reset(); }

  ChannelCallbackHandle handle;
  folly::IntrusiveListHook hook;
};

template <typename TValue, typename OnNextFunc>
class ChannelCallbackProcessorImplWithList;
} // namespace detail

/**
 * A list of channel callback handles. When consumeChannelWithCallback is
 * invoked with a list, a cancellation handle is automatically added to the list
 * for the consumption operation. Similarly, when a consumption operation is
 * completed, the handle is automatically removed from the lists.
 *
 * If the list still has any cancellation handles remaining when the list is
 * destroyed, cancellation is triggered for each handle in the list.
 *
 * This list is not thread safe.
 */
class ChannelCallbackHandleList {
 public:
  ChannelCallbackHandleList() {}

  ChannelCallbackHandleList(ChannelCallbackHandleList&& other) noexcept {
    holders_.swap(other.holders_);
  }

  ChannelCallbackHandleList& operator=(
      ChannelCallbackHandleList&& other) noexcept {
    if (&other == this) {
      return *this;
    }
    holders_.swap(other.holders_);
    return *this;
  }

  ~ChannelCallbackHandleList() { clear(); }

  void clear() {
    for (auto& holder : holders_) {
      holder.requestCancellation();
    }
    holders_.clear();
  }

 private:
  template <typename TValue, typename OnNextFunc>
  friend class detail::ChannelCallbackProcessorImplWithList;

  void add(detail::ChannelCallbackHandleHolder& holder) {
    holders_.push_back(holder);
  }

  using ChannelCallbackHandleListImpl = folly::IntrusiveList<
      detail::ChannelCallbackHandleHolder,
      &detail::ChannelCallbackHandleHolder::hook>;

  ChannelCallbackHandleListImpl holders_;
};
} // namespace channels
} // namespace folly
