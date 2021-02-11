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
#include <folly/experimental/coro/Coroutine.h>
#include <folly/experimental/coro/Invoke.h>
#include <folly/experimental/coro/UnboundedQueue.h>
#include <folly/experimental/coro/ViaIfAsync.h>
#include <folly/fibers/Semaphore.h>

#include <memory>
#include <utility>

#if FOLLY_HAS_COROUTINES

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
    auto cancellationSource = std::optional<folly::CancellationSource>();
    auto onClosedCallback = std::unique_ptr<OnClosedCallback>();
    if (onClosed != nullptr) {
      cancellationSource.emplace();
      onClosedCallback = std::make_unique<OnClosedCallback>(
          cancellationSource.value(), std::move(onClosed));
    }
    auto guard =
        folly::makeGuard([cancellationSource = std::move(cancellationSource)] {
          if (cancellationSource) {
            cancellationSource->requestCancellation();
          }
        });
    return {
        folly::coro::co_invoke(
            [queue,
             guard = std::move(guard)]() -> folly::coro::AsyncGenerator<T&&> {
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
        folly::CancellationSource cancellationSource,
        folly::Function<void()> onClosedFunc)
        : cancellationSource_(std::move(cancellationSource)),
          cancellationCallback_(
              cancellationSource_.getToken(), std::move(onClosedFunc)) {}

    void requestInvoke() { cancellationSource_.requestCancellation(); }

    bool wasInvokeRequested() {
      return cancellationSource_.isCancellationRequested();
    }

   private:
    folly::CancellationSource cancellationSource_;
    folly::CancellationCallback cancellationCallback_;
  };

  explicit AsyncPipe(
      std::weak_ptr<Queue> queue, std::unique_ptr<OnClosedCallback> onClosed)
      : queue_(std::move(queue)), onClosed_(std::move(onClosed)) {}

  std::weak_ptr<Queue> queue_;
  std::unique_ptr<OnClosedCallback> onClosed_;
};

// Bounded variant of AsyncPipe which buffers a fixed number of writes
// before blocking new attempts to write until the buffer is drained.
//
// Usage:
//   auto [generator, pipe] = BoundedAsyncPipe<T>::create(/* tokens */ 10);
//   co_await pipe.write(std::move(entry));
//   auto entry = co_await generator.next().value();
//
// write() is a coroutine which only blocks when
// no capacity is remaining. write() returns false if the read-end has been
// destroyed or was destroyed while blocking, only throwing OperationCanceled
// if the parent coroutine was canceled while blocking.
//
// try_write() is offered which will never block, but will return false
// and not write if no capacity is remaining or the read end is already
// destroyed.
//
// close() functions the same as AsyncPipe, and must be invoked before
// destruction if an onClose callback is attached.
template <typename T, bool SingleProducer = true>
class BoundedAsyncPipe {
 public:
  using Pipe = AsyncPipe<T, SingleProducer>;

  static std::pair<AsyncGenerator<T&&>, BoundedAsyncPipe> create(
      size_t tokens, folly::Function<void()> onClosed = nullptr) {
    auto [generator, pipe] = Pipe::create(std::move(onClosed));

    auto semaphore = std::make_shared<folly::fibers::Semaphore>(tokens);

    folly::CancellationSource cancellationSource;
    auto cancellationToken = cancellationSource.getToken();
    auto guard = folly::makeGuard(
        [cancellationSource = std::move(cancellationSource)]() {
          cancellationSource.requestCancellation();
        });

    auto signalingGenerator = co_invoke(
        [generator = std::move(generator),
         guard = std::move(guard),
         semaphore]() mutable -> folly::coro::AsyncGenerator<T&&> {
          while (true) {
            auto itemTry = co_await co_awaitTry(generator.next());
            semaphore->signal();
            co_yield co_result(std::move(itemTry));
          }
        });
    return std::pair<AsyncGenerator<T&&>, BoundedAsyncPipe>(
        std::move(signalingGenerator),
        BoundedAsyncPipe(
            std::move(pipe),
            std::move(semaphore),
            std::move(cancellationToken)));
  }

  template <typename U = T>
  folly::coro::Task<bool> write(U&& u) {
    auto parentToken = co_await co_current_cancellation_token;

    auto waitResult = co_await co_awaitTry(co_withCancellation(
        folly::CancellationToken::merge(
            std::move(parentToken), cancellationToken_),
        semaphore_->co_wait()));
    if (cancellationToken_.isCancellationRequested()) {
      // eagerly return false if the read-end was destroyed instead of throwing
      // OperationCanceled, to have uniform behavior when the generator is
      // destroyed
      co_return false;
    } else if (waitResult.hasException()) {
      co_yield co_error(std::move(waitResult).exception());
    }

    co_return pipe_.write(std::forward<U>(u));
  }

  template <typename U = T>
  bool try_write(U&& u) {
    bool available = semaphore_->try_wait();
    if (!available) {
      return false;
    }
    return pipe_.write(std::forward<U>(u));
  }

  void close(exception_wrapper&& w) && { std::move(pipe_).close(std::move(w)); }
  void close() && { std::move(pipe_).close(); }

 private:
  BoundedAsyncPipe(
      Pipe&& pipe,
      std::shared_ptr<folly::fibers::Semaphore> semaphore,
      folly::CancellationToken cancellationToken)
      : pipe_(std::move(pipe)),
        semaphore_(std::move(semaphore)),
        cancellationToken_(std::move(cancellationToken)) {}

  Pipe pipe_;
  std::shared_ptr<folly::fibers::Semaphore> semaphore_;
  folly::CancellationToken cancellationToken_;
};

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
