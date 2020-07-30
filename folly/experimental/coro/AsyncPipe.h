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

#include <folly/Try.h>
#include <folly/experimental/coro/AsyncGenerator.h>
#include <folly/experimental/coro/Invoke.h>
#include <folly/experimental/coro/UnboundedQueue.h>

#include <memory>
#include <utility>

namespace folly {
namespace coro {

// An AsyncGenerator with a write end
//
// Usage:
//   auto pipe = AsyncPipe<T>::create();
//   pipe.second.write(std::move(val1));
//   auto val2 = co_await pipe.first.next();
//
//  write() returns false if the read end has been destroyed
//  The generator is completed when the write end is destroyed or on close()
//  close() can also be passed an exception, which is thrown when read

template <typename T>
class AsyncPipe {
 public:
  ~AsyncPipe() {
    std::move(*this).close();
  }
  AsyncPipe(AsyncPipe&& pipe) noexcept {
    queue_ = std::move(pipe.queue_);
  }
  AsyncPipe& operator=(AsyncPipe&& pipe) {
    if (this != &pipe) {
      std::move(*this).close();
      queue_ = std::move(pipe.queue_);
    }
    return *this;
  }

  static std::pair<folly::coro::AsyncGenerator<T&&>, AsyncPipe<T>> create() {
    auto queue = std::make_shared<Queue>();
    return {
        folly::coro::co_invoke([queue]() -> folly::coro::AsyncGenerator<T&&> {
          while (true) {
            auto val = co_await queue->dequeue();
            if (val.hasValue() || val.hasException()) {
              co_yield std::move(*val);
            } else {
              co_return;
            }
          }
        }),
        AsyncPipe(queue)};
  }

  template <typename U = T>
  bool write(U&& val) {
    if (auto queue = queue_.lock()) {
      queue->enqueue(folly::Try<T>(std::forward<U>(val)));
      return true;
    }
    return false;
  }

  void close(folly::exception_wrapper ew) && {
    if (auto queue = queue_.lock()) {
      queue->enqueue(folly::Try<T>(std::move(ew)));
      queue_.reset();
    }
  }
  void close() && {
    if (auto queue = queue_.lock()) {
      queue->enqueue(folly::Try<T>());
      queue_.reset();
    }
  }

 private:
  using Queue = folly::coro::UnboundedQueue<folly::Try<T>, true, true>;
  explicit AsyncPipe(std::weak_ptr<Queue> queue) : queue_(std::move(queue)) {}
  std::weak_ptr<Queue> queue_;
};

} // namespace coro
} // namespace folly
