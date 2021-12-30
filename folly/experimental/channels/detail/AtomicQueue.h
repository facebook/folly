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

#include <atomic>
#include <cassert>
#include <memory>
#include <utility>
#include <glog/logging.h>

#include <folly/lang/Assume.h>

namespace folly {
namespace channels {
namespace detail {

template <typename T>
class Queue {
 public:
  constexpr Queue() noexcept {}
  constexpr Queue(Queue&& other) noexcept
      : head_(std::exchange(other.head_, nullptr)) {}
  Queue& operator=(Queue&& other) noexcept {
    clear();
    std::swap(head_, other.head_);
    return *this;
  }
  ~Queue() { clear(); }

  bool empty() const noexcept { return !head_; }

  T& front() noexcept { return head_->value; }

  void pop() noexcept {
    std::unique_ptr<Node>(std::exchange(head_, head_->next));
  }

  void clear() {
    while (!empty()) {
      pop();
    }
  }

  explicit operator bool() const { return !empty(); }

  struct Node {
    explicit Node(T&& t) : value(std::move(t)) {}

    T value;
    Node* next{nullptr};
  };

  constexpr explicit Queue(Node* head) noexcept : head_(head) {}
  static Queue fromReversed(Node* tail) noexcept {
    // Reverse a linked list.
    Node* head{nullptr};
    while (tail) {
      head = std::exchange(tail, std::exchange(tail->next, head));
    }
    return Queue(head);
  }

  Node* head_{nullptr};
};

template <typename Consumer, typename Message>
class AtomicQueue {
 public:
  using MessageQueue = Queue<Message>;

  AtomicQueue() {}
  ~AtomicQueue() {
    auto storage = storage_.load(std::memory_order_acquire);
    auto type = static_cast<Type>(storage & kTypeMask);
    auto ptr = storage & kPointerMask;
    switch (type) {
      case Type::EMPTY:
      case Type::CLOSED:
        return;
      case Type::TAIL:
        MessageQueue::fromReversed(
            reinterpret_cast<typename MessageQueue::Node*>(ptr));
        return;
      case Type::CONSUMER:
      default:
        folly::assume_unreachable();
    };
  }
  AtomicQueue(const AtomicQueue&) = delete;
  AtomicQueue& operator=(const AtomicQueue&) = delete;

  template <typename... ConsumerArgs>
  void push(Message&& value, ConsumerArgs&&... consumerArgs) {
    std::unique_ptr<typename MessageQueue::Node> node(
        new typename MessageQueue::Node(std::move(value)));
    assert(!(reinterpret_cast<intptr_t>(node.get()) & kTypeMask));

    auto storage = storage_.load(std::memory_order_relaxed);
    while (true) {
      auto type = static_cast<Type>(storage & kTypeMask);
      auto ptr = storage & kPointerMask;
      switch (type) {
        case Type::EMPTY:
        case Type::TAIL:
          node->next = reinterpret_cast<typename MessageQueue::Node*>(ptr);
          if (storage_.compare_exchange_weak(
                  storage,
                  reinterpret_cast<intptr_t>(node.get()) |
                      static_cast<intptr_t>(Type::TAIL),
                  std::memory_order_release,
                  std::memory_order_relaxed)) {
            node.release();
            return;
          }
          break;
        case Type::CLOSED:
          return;
        case Type::CONSUMER:
          node->next = nullptr;
          if (storage_.compare_exchange_weak(
                  storage,
                  reinterpret_cast<intptr_t>(node.get()) |
                      static_cast<intptr_t>(Type::TAIL),
                  std::memory_order_acq_rel,
                  std::memory_order_relaxed)) {
            node.release();
            auto consumer = reinterpret_cast<Consumer*>(ptr);
            consumer->consume(std::forward<ConsumerArgs>(consumerArgs)...);
            return;
          }
          break;
        default:
          folly::assume_unreachable();
      }
    }
  }

  template <typename... ConsumerArgs>
  bool wait(Consumer* consumer, ConsumerArgs&&... consumerArgs) {
    assert(!(reinterpret_cast<intptr_t>(consumer) & kTypeMask));
    auto storage = storage_.load(std::memory_order_relaxed);
    while (true) {
      auto type = static_cast<Type>(storage & kTypeMask);
      switch (type) {
        case Type::EMPTY:
          if (storage_.compare_exchange_weak(
                  storage,
                  reinterpret_cast<intptr_t>(consumer) |
                      static_cast<intptr_t>(Type::CONSUMER),
                  std::memory_order_release,
                  std::memory_order_relaxed)) {
            return true;
          }
          break;
        case Type::CLOSED:
          consumer->canceled(std::forward<ConsumerArgs>(consumerArgs)...);
          return true;
        case Type::TAIL:
          return false;
        case Type::CONSUMER:
        default:
          folly::assume_unreachable();
      }
    }
  }

  template <typename... ConsumerArgs>
  void close(ConsumerArgs&&... consumerArgs) {
    auto storage = storage_.exchange(
        static_cast<intptr_t>(Type::CLOSED), std::memory_order_acquire);
    auto type = static_cast<Type>(storage & kTypeMask);
    auto ptr = storage & kPointerMask;
    switch (type) {
      case Type::EMPTY:
        return;
      case Type::TAIL:
        MessageQueue::fromReversed(
            reinterpret_cast<typename MessageQueue::Node*>(ptr));
        return;
      case Type::CONSUMER:
        reinterpret_cast<Consumer*>(ptr)->canceled(
            std::forward<ConsumerArgs>(consumerArgs)...);
        return;
      case Type::CLOSED:
      default:
        folly::assume_unreachable();
    };
  }

  bool isClosed() {
    auto type = static_cast<Type>(storage_ & kTypeMask);
    return type == Type::CLOSED;
  }

  template <typename... ConsumerArgs>
  MessageQueue getMessages(ConsumerArgs&&... consumerArgs) {
    auto storage = storage_.exchange(
        static_cast<intptr_t>(Type::EMPTY), std::memory_order_acquire);
    auto type = static_cast<Type>(storage & kTypeMask);
    auto ptr = storage & kPointerMask;
    switch (type) {
      case Type::TAIL:
        return MessageQueue::fromReversed(
            reinterpret_cast<typename MessageQueue::Node*>(ptr));
      case Type::EMPTY:
        return MessageQueue();
      case Type::CLOSED:
        // We accidentally re-opened the queue, so close it again.
        // This is only safe to do because isClosed() can't be called
        // concurrently with getMessages().
        close(std::forward<ConsumerArgs>(consumerArgs)...);
        return MessageQueue();
      case Type::CONSUMER:
      default:
        folly::assume_unreachable();
    };
  }

  Consumer* cancelCallback() {
    auto storage = storage_.load(std::memory_order_acquire);
    while (true) {
      auto type = static_cast<Type>(storage & kTypeMask);
      auto ptr = storage & kPointerMask;
      switch (type) {
        case Type::CONSUMER:
          if (storage_.compare_exchange_weak(
                  storage,
                  static_cast<intptr_t>(Type::EMPTY),
                  std::memory_order_relaxed,
                  std::memory_order_relaxed)) {
            return reinterpret_cast<Consumer*>(ptr);
          }
          break;
        case Type::TAIL:
        case Type::EMPTY:
        case Type::CLOSED:
        default:
          return nullptr;
      }
    }
  }

 private:
  enum class Type : intptr_t { EMPTY = 0, CONSUMER = 1, TAIL = 2, CLOSED = 3 };

  static constexpr intptr_t kTypeMask = 3;
  static constexpr intptr_t kPointerMask = ~kTypeMask;

  std::atomic<intptr_t> storage_{0};
};
} // namespace detail
} // namespace channels
} // namespace folly
