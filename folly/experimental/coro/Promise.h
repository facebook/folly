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

#include <utility>

#include <folly/CancellationToken.h>
#include <folly/Try.h>
#include <folly/experimental/coro/Baton.h>
#include <folly/experimental/coro/Coroutine.h>
#include <folly/futures/Promise.h>
#include <folly/synchronization/RelaxedAtomic.h>

#if FOLLY_HAS_COROUTINES

namespace folly::coro {
template <typename T>
class Promise;
template <typename T>
class Future;

// Creates promise and associated unfulfilled future
template <typename T>
std::pair<Promise<T>, Future<T>> makePromiseContract();

// Creates fulfilled future
template <typename T>
Future<remove_cvref_t<T>> makeFuture(T&&);
template <typename T>
Future<T> makeFuture(exception_wrapper&&);
Future<void> makeFuture();

namespace detail {
template <typename T>
struct PromiseState {
  PromiseState() = default;

  Try<T> result;
  // Must be exchanged to true before setting result
  folly::relaxed_atomic<bool> fulfilled{false};
  // Must be posted after setting result
  coro::Baton ready;
};
} // namespace detail

template <typename T>
class Promise {
 public:
  /**
   * Construct an empty Promise.
   *
   * This object is not valid use until you initialize it with move assignment.
   */
  Promise() = default;

  Promise(Promise&& other) noexcept
      : ct_(std::move(other.ct_)),
        state_(std::exchange(other.state_, nullptr)) {}
  Promise& operator=(Promise&& other) noexcept {
    if (this != &other && state_ && !state_->fulfilled) {
      setException(BrokenPromise{tag<T>});
    }
    ct_ = std::move(other.ct_);
    state_ = std::exchange(other.state_, nullptr);
    return *this;
  }
  Promise(const Promise&) = delete;
  Promise& operator=(const Promise&) = delete;

  ~Promise() {
    if (state_ && !state_->fulfilled) {
      setException(BrokenPromise{tag<T>});
    }
  }

  bool valid() const noexcept { return state_; }

  bool isFulfilled() const noexcept { return state_ && state_->fulfilled; }

  template <typename... Args>
  void setValue(Args&&... args) {
    trySetValue(std::forward<Args>(args)...);
  }

  template <typename... Args>
  void setException(Args&&... args) {
    trySetException(std::forward<Args>(args)...);
  }

  void setResult(Try<T>&& result) { trySetResult(std::move(result)); }

  /**
   * Fulfills the promise with a value if not already fulfilled.
   * @returns Whether the fulfillment took place.
   */
  template <typename... Args>
  bool trySetValue(Args&&... args) {
    DCHECK(state_);
    if (state_->fulfilled.exchange(true)) {
      return false;
    }
    if constexpr (std::is_void_v<T>) {
      static_assert(sizeof...(Args) == 0);
    } else {
      state_->result.emplace(std::forward<Args>(args)...);
    }
    state_->ready.post();
    return true;
  }

  /**
   * Fulfills the promise with an exception if not already fulfilled.
   * @returns Whether the fulfillment took place.
   */
  template <typename... Args>
  bool trySetException(Args&&... args) {
    DCHECK(state_);
    if (state_->fulfilled.exchange(true)) {
      return false;
    }
    state_->result.emplaceException(std::forward<Args>(args)...);
    state_->ready.post();
    return true;
  }

  /**
   * Fulfills the promise with a Try if not already fulfilled.
   * @returns Whether the fulfillment took place.
   */
  bool trySetResult(Try<T>&& result) {
    DCHECK(state_);
    if (state_->fulfilled.exchange(true)) {
      return false;
    }
    state_->result = std::move(result);
    state_->ready.post();
    return true;
  }

  /**
   * Fulfills the promise with the result of func if not already fulfilled.
   *
   * If either the call to func or the result's constructor completes with an
   * exception then the exception is caught and stored as the result.
   *
   * @returns Whether the fulfillment took place.
   */
  template <typename Func>
  bool trySetWith(Func&& func) {
    DCHECK(state_);
    if (state_->fulfilled.exchange(true)) {
      return false;
    }
    try {
      if constexpr (std::is_constructible_v<T, invoke_result_t<Func>>) {
        state_->result.emplace(std::forward<Func>(func)());
      } else if constexpr (std::is_constructible_v<
                               exception_wrapper,
                               invoke_result_t<Func>>) {
        state_->result.emplaceException(std::forward<Func>(func)());
      } else if constexpr (std::
                               is_assignable_v<Try<T>, invoke_result_t<Func>>) {
        state_->result = std::forward<Func>(func)();
      } else {
        static_assert(always_false<Func>);
      }
    } catch (...) {
      state_->result.emplaceException(std::current_exception());
    }
    state_->ready.post();
    return true;
  }

  const CancellationToken& getCancellationToken() const { return ct_; }

 private:
  Promise(CancellationToken ct, detail::PromiseState<T>& state)
      : ct_(std::move(ct)), state_(&state) {}

  CancellationToken ct_;
  detail::PromiseState<T>* state_{nullptr};

  friend std::pair<Promise<T>, Future<T>> makePromiseContract<T>();
};

template <typename T>
class Future {
 public:
  /**
   * Construct an empty Future.
   *
   * This object is not valid use until you initialize it with move assignment.
   */
  Future() = default;

  Future(Future&&) noexcept = default;
  Future& operator=(Future&&) noexcept = default;
  Future(const Future&) = delete;
  Future& operator=(const Future&) = delete;

  class WaitOperation : private Baton::WaitOperation {
   public:
    explicit WaitOperation(Future& future) noexcept
        : Baton::WaitOperation(future.state_->ready),
          future_(future),
          cb_(std::move(future.ct_), [&] { future_.cancel(); }) {}

    using Baton::WaitOperation::await_ready;
    using Baton::WaitOperation::await_suspend;

    T await_resume() {
      if constexpr (!std::is_void_v<T>) {
        return std::move(future_.state_->result.value());
      } else {
        future_.state_->result.throwIfFailed();
      }
    }

    folly::Try<T> await_resume_try() {
      return std::move(future_.state_->result);
    }

   private:
    Future& future_;
    CancellationCallback cb_;
  };

  [[nodiscard]] WaitOperation operator co_await() && noexcept {
    return WaitOperation{*this};
  }

  bool isReady() const noexcept { return state_->ready.ready(); }

  friend Future co_withCancellation(
      folly::CancellationToken ct, Future&& future) noexcept {
    if (!std::exchange(future.hasCancelTokenOverride_, true)) {
      future.ct_ = std::move(ct);
    }
    return std::move(future);
  }

 private:
  Future(CancellationSource cs, detail::PromiseState<T>& state)
      : cs_(std::move(cs)), state_(&state) {}

  void cancel() {
    if (!state_->fulfilled.exchange(true)) {
      cs_.requestCancellation();
      state_->result.emplaceException(OperationCancelled{});
      state_->ready.post();
    }
  }

  CancellationSource cs_;
  detail::PromiseState<T>* state_{nullptr};
  // The token inherited when the future is awaited
  CancellationToken ct_;
  bool hasCancelTokenOverride_{false};

  friend std::pair<Promise<T>, Future<T>> makePromiseContract<T>();
};

/**
 * makePromiseContract can help you migrating your non-coroutine code base to
 * coroutine. If your code already uses Future/SemiFuture, you don't need this
 * tool. A common use case is with async callback functions. In the example, we
 * can pass a callback function into the legacy code sleepAndNotify and
 * sleepAndNotify sets the promise on completion. Consider to use detachOnCancel
 * with this makePromiseContract to handle long running (longer than your
 * timeout) tasks that don't handle cancellation properly.
 *
 * \refcode folly/docs/examples/folly/experimental/coro/Promise.cpp
 */
template <typename T>
std::pair<Promise<T>, Future<T>> makePromiseContract() {
  auto [cs, data] = CancellationSource::create(
      folly::detail::WithDataTag<detail::PromiseState<T>>{});
  return {
      Promise<T>{cs.getToken(), std::get<0>(*data)},
      Future<T>{std::move(cs), std::get<0>(*data)}};
}

template <typename T>
Future<remove_cvref_t<T>> makeFuture(T&& t) {
  auto [promise, future] = makePromiseContract<remove_cvref_t<T>>();
  promise.setValue(std::forward<T>(t));
  return std::move(future);
}
template <typename T>
Future<T> makeFuture(exception_wrapper&& ex) {
  auto [promise, future] = makePromiseContract<T>();
  promise.setException(std::move(ex));
  return std::move(future);
}
inline Future<void> makeFuture() {
  auto [promise, future] = makePromiseContract<void>();
  promise.setValue();
  return std::move(future);
}

} // namespace folly::coro

#endif
