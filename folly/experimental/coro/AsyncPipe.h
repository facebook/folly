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
//  write() returns false if the read end has been destroyed (unless
//  SingleProducer is disabled, in which case this behavior is undefined).
//  The generator is completed when the write end is destroyed or on close()
//  close() can also be passed an exception, which is thrown when read.
//
//  An optional onClosed callback can be passed to create(). This callback will
//  be called either when the generator is destroyed by the consumer, or when
//  the pipe is closed by the publisher (whichever comes first). The onClosed
//  callback may destroy the AsyncPipe object inline, and must not call close()
//  on the AsyncPipe object inline. If an onClosed callback is specified and the
//  publisher would like to destroy the pipe outside of the callback, it must
//  first close the pipe.
//
//  If SingleProducer is disabled, AsyncPipe's write() method (but not its
//  close() method) becomes thread-safe. close() must be sequenced after all
//  write()s in this mode.

template <typename T, bool SingleProducer = true>
class AsyncPipe {
 public:
  ~AsyncPipe() {
    CHECK(!onClosed_ || onClosed_->wasInvokeRequested())
        << "If an onClosed callback is specified and the generator still "
        << "exists, the publisher must explicitly close the pipe prior to "
        << "destruction.";
    std::move(*this).close();
  }

  AsyncPipe(AsyncPipe&& pipe) noexcept {
    queue_ = std::move(pipe.queue_);
    onClosed_ = std::move(pipe.onClosed_);
  }

  AsyncPipe& operator=(AsyncPipe&& pipe) {
    if (this != &pipe) {
      CHECK(!onClosed_ || onClosed_->wasInvokeRequested())
          << "If an onClosed callback is specified and the generator still "
          << "exists, the publisher must explicitly close the pipe prior to "
          << "destruction.";
      std::move(*this).close();
      queue_ = std::move(pipe.queue_);
      onClosed_ = std::move(pipe.onClosed_);
    }
    return *this;
  }

  static std::pair<folly::coro::AsyncGenerator<T&&>, AsyncPipe> create(
      folly::Function<void()> onClosed = nullptr) {
    auto queue = std::make_shared<Queue>();
    auto cancellationSource = std::shared_ptr<folly::CancellationSource>();
    auto onClosedCallback = std::unique_ptr<OnClosedCallback>();
    if (onClosed != nullptr) {
      cancellationSource = std::make_shared<folly::CancellationSource>();
      onClosedCallback = std::make_unique<OnClosedCallback>(
          cancellationSource, std::move(onClosed));
    }
    auto guard = folly::makeGuard([cancellationSource] {
      if (cancellationSource != nullptr) {
        cancellationSource->requestCancellation();
      }
    });
    return {
        folly::coro::co_invoke(
            [ queue,
              guard = std::move(guard) ]() -> folly::coro::AsyncGenerator<T&&> {
              while (true) {
                auto val = co_await queue->dequeue();
                if (val.hasValue() || val.hasException()) {
                  co_yield std::move(*val);
                } else {
                  co_return;
                }
              }
            }),
        AsyncPipe(queue, std::move(onClosedCallback))};
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
    if (onClosed_ != nullptr) {
      onClosed_->requestInvoke();
      onClosed_.reset();
    }
  }

  void close() && {
    if (auto queue = queue_.lock()) {
      queue->enqueue(folly::Try<T>());
      queue_.reset();
    }
    if (onClosed_ != nullptr) {
      onClosed_->requestInvoke();
      onClosed_.reset();
    }
  }

 private:
  using Queue =
      folly::coro::UnboundedQueue<folly::Try<T>, SingleProducer, true>;

  class OnClosedCallback {
   public:
    OnClosedCallback(
        std::shared_ptr<folly::CancellationSource> cancellationSource,
        folly::Function<void()> onClosedFunc)
        : cancellationSource_(std::move(cancellationSource)),
          cancellationCallback_(
              cancellationSource_->getToken(),
              std::move(onClosedFunc)) {}

    void requestInvoke() { cancellationSource_->requestCancellation(); }

    bool wasInvokeRequested() {
      return cancellationSource_->isCancellationRequested();
    }

   private:
    std::shared_ptr<folly::CancellationSource> cancellationSource_;
    folly::CancellationCallback cancellationCallback_;
  };

  explicit AsyncPipe(
      std::weak_ptr<Queue> queue,
      std::unique_ptr<OnClosedCallback> onClosed)
      : queue_(std::move(queue)), onClosed_(std::move(onClosed)) {}

  std::weak_ptr<Queue> queue_;
  std::unique_ptr<OnClosedCallback> onClosed_;
};

} // namespace coro
} // namespace folly
