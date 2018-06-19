/*
 * Copyright 2017-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <experimental/coroutine>
#include <future>

#include <folly/Optional.h>
#include <folly/experimental/coro/Wait.h>
#include <folly/futures/Future.h>

namespace folly {
namespace coro {

template <typename T>
class AwaitableReady {
 public:
  explicit AwaitableReady(T value) : value_(std::move(value)) {}

  bool await_ready() {
    return true;
  }

  bool await_suspend(std::experimental::coroutine_handle<>) {
    return false;
  }

  T await_resume() {
    return std::move(value_);
  }

 private:
  T value_;
};

struct getCurrentExecutor {};

struct yield {
  bool await_ready() {
    return false;
  }

  void await_suspend(std::experimental::coroutine_handle<> ch) {
    ch();
  }

  void await_resume() {}
};

template <typename Awaitable>
class TimedWaitAwaitable {
 public:
  static_assert(
      std::is_same<Awaitable, std::decay_t<Awaitable>>::value,
      "Awaitable should be decayed.");
  using await_resume_return_type =
      decltype((operator co_await(std::declval<Awaitable>())).await_resume());

  TimedWaitAwaitable(Awaitable&& awaitable, std::chrono::milliseconds duration)
      : awaitable_(std::move(awaitable)), duration_(duration) {}

  bool await_ready() {
    return false;
  }

  bool await_suspend(std::experimental::coroutine_handle<> ch) {
    auto sharedState = std::make_shared<SharedState>(ch, storage_);
    waitAndNotify(std::move(awaitable_), sharedState).detach();
    futures::sleep(duration_).then(
        [sharedState = std::move(sharedState)] { sharedState->setTimeout(); });
    return true;
  }

  Optional<await_resume_return_type> await_resume() {
    if (!storage_.hasValue() && !storage_.hasException()) {
      return folly::none;
    }
    return std::move(storage_).value();
  }

 private:
  class SharedState {
   public:
    SharedState(
        std::experimental::coroutine_handle<> ch,
        Try<await_resume_return_type>& storage)
        : ch_(std::move(ch)), storage_(storage) {}

    void setValue(await_resume_return_type&& value) {
      if (first_.exchange(true, std::memory_order_relaxed)) {
        return;
      }
      assume(!storage_.hasValue() && !storage_.hasException());
      storage_ = Try<await_resume_return_type>(std::move(value));
      ch_();
    }

    void setException(exception_wrapper e) {
      if (first_.exchange(true, std::memory_order_relaxed)) {
        return;
      }
      assume(!storage_.hasValue() && !storage_.hasException());
      storage_ = Try<await_resume_return_type>(std::move(e));
      ch_();
    }

    void setTimeout() {
      if (first_.exchange(true, std::memory_order_relaxed)) {
        return;
      }
      ch_();
    }

   private:
    std::atomic<bool> first_{false};
    std::experimental::coroutine_handle<> ch_;
    Try<await_resume_return_type>& storage_;
  };

  static Wait waitAndNotify(
      Awaitable awaitable,
      std::shared_ptr<SharedState> sharedState) {
    try {
      sharedState->setValue(co_await awaitable);
    } catch (const std::exception& e) {
      sharedState->setException(exception_wrapper(std::current_exception(), e));
    } catch (...) {
      sharedState->setException(exception_wrapper(std::current_exception()));
    }
  }

  Awaitable awaitable_;
  std::chrono::milliseconds duration_;
  Try<await_resume_return_type> storage_;
};

template <typename Awaitable>
TimedWaitAwaitable<std::decay_t<Awaitable>> timed_wait(
    Awaitable&& awaitable,
    std::chrono::milliseconds duration) {
  return TimedWaitAwaitable<std::decay_t<Awaitable>>(
      std::forward<Awaitable>(awaitable), duration);
}

} // namespace coro
} // namespace folly
