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

#include <folly/ExceptionString.h>
#include <folly/Executor.h>

namespace folly {
namespace coro {

template <typename Awaitable>
class AwaitWrapper {
 public:
  struct promise_type {
    std::experimental::suspend_always initial_suspend() {
      return {};
    }

    std::experimental::suspend_never final_suspend() {
      executor_->add(awaiter_);
      awaitWrapper_->promise_ = nullptr;
      return {};
    }

    void return_void() {}

    void unhandled_exception() {
      LOG(FATAL) << "Failed to schedule a task to awake a coroutine: "
                 << exceptionStr(std::current_exception());
    }

    AwaitWrapper get_return_object() {
      return {*this};
    }

    Executor* executor_;
    std::experimental::coroutine_handle<> awaiter_;
    AwaitWrapper* awaitWrapper_{nullptr};
  };

  static AwaitWrapper create(Awaitable* awaitable) {
    return {awaitable};
  }

  static AwaitWrapper create(Awaitable* awaitable, Executor* executor) {
    auto ret = awaitWrapper();
    ret.awaitable_ = awaitable;
    ret.promise_->executor_ = executor;
    return ret;
  }

  bool await_ready() {
    return awaitable_->await_ready();
  }

  using await_suspend_return_type =
      decltype((*static_cast<Awaitable*>(nullptr))
                   .await_suspend(std::experimental::coroutine_handle<>()));

  await_suspend_return_type await_suspend(
      std::experimental::coroutine_handle<> awaiter) {
    if (promise_) {
      promise_->awaiter_ = std::move(awaiter);
      return awaitable_->await_suspend(
          std::experimental::coroutine_handle<promise_type>::from_promise(
              *promise_));
    }

    return awaitable_->await_suspend(awaiter);
  }

  decltype((*static_cast<Awaitable*>(nullptr)).await_resume()) await_resume() {
    return awaitable_->await_resume();
  }

  ~AwaitWrapper() {
    if (promise_) {
      // This happens if await_ready() returns true or await_suspend() returns
      // false.
      std::experimental::coroutine_handle<promise_type>::from_promise(*promise_)
          .destroy();
    }
  }

 private:
  AwaitWrapper(Awaitable* awaitable) : awaitable_(awaitable) {}
  AwaitWrapper(promise_type& promise) : promise_(&promise) {
    promise.awaitWrapper_ = this;
  }

  static AwaitWrapper awaitWrapper() {
    co_return;
  }

  promise_type* promise_{nullptr};
  Awaitable* awaitable_{nullptr};
};
} // namespace coro
} // namespace folly
