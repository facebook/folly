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

#include <folly/experimental/channels/detail/AtomicQueue.h>
#include <folly/experimental/coro/Baton.h>
#include <folly/experimental/coro/Coroutine.h>
#include <folly/experimental/coro/Mutex.h>
#include <folly/experimental/coro/Task.h>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {
namespace detail {
template <bool UseMutex>
struct SmallUnboundedQueueBase {
  auto co_scoped_lock() { return ready_awaitable(true); }
};
template <>
struct SmallUnboundedQueueBase<true> {
  auto co_scoped_lock() { return mutex_.co_scoped_lock(); }
  folly::coro::Mutex mutex_;
};
} // namespace detail

// Alternative to coro::UnboundedQueue with much smaller memory size when empty
// but lower throughput.
// Substantially worse in multi-consumer case.
// Only supports enqueue(T) and dequeue().

template <typename T, bool SingleProducer = false, bool SingleConsumer = false>
class SmallUnboundedQueue : detail::SmallUnboundedQueueBase<!SingleConsumer> {
  struct Consumer {
    void consume() { baton.post(); }
    void canceled() { std::terminate(); }
    folly::coro::Baton baton;
  };

 public:
  ~SmallUnboundedQueue() { queue_.close(); }

  template <typename U = T>
  void enqueue(U&& val) {
    queue_.push(T(std::forward<U>(val)));
  }

  folly::coro::Task<T> dequeue() {
    FOLLY_MAYBE_UNUSED auto maybeLock = co_await this->co_scoped_lock();
    if (buffer_.empty()) {
      Consumer c;
      if (queue_.wait(&c)) {
        bool cancelled = false;
        CancellationCallback cb(co_await co_current_cancellation_token, [&] {
          if (queue_.cancelCallback()) {
            cancelled = true;
            c.baton.post();
          }
        });
        co_await c.baton;
        if (cancelled) {
          co_yield co_cancelled;
        }
      }
      buffer_ = queue_.getMessages();
      DCHECK(!buffer_.empty());
    }
    SCOPE_EXIT { buffer_.pop(); };
    co_return std::move(buffer_.front());
  }

 private:
  folly::channels::detail::AtomicQueue<Consumer, T> queue_;
  folly::channels::detail::Queue<T> buffer_;
};

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
