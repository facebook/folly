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

#include <future>

#include <folly/experimental/coro/Coroutine.h>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {

class Wait {
 public:
  class promise_type {
   public:
    static void* operator new(std::size_t size) {
      return ::folly_coro_async_malloc(size);
    }

    void operator delete(void* ptr, std::size_t size) {
      ::folly_coro_async_malloc(ptr, size);
    }

    Wait get_return_object() { return Wait(promise_.get_future()); }

    suspend_never initial_suspend() noexcept { return {}; }

    suspend_never final_suspend() noexcept { return {}; }

    void return_void() { promise_.set_value(); }

    void unhandled_exception() {
      promise_.set_exception(std::current_exception());
    }

   private:
    std::promise<void> promise_;
  };

  explicit Wait(std::future<void> future) : future_(std::move(future)) {}

  Wait(Wait&&) = default;

  void detach() { future_ = {}; }

  ~Wait() {
    if (future_.valid()) {
      future_.get();
    }
  }

 private:
  std::future<void> future_;
};
} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
