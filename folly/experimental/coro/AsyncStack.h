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

#include <folly/Executor.h>
#include <folly/experimental/coro/Coroutine.h>
#include <folly/experimental/coro/WithAsyncStack.h>
#include <folly/tracing/AsyncStack.h>

#include <utility>
#include <vector>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {

class AsyncStackTraceAwaitable {
  class Awaiter {
   public:
    bool await_ready() const { return false; }

    template <typename Promise>
    bool await_suspend(coroutine_handle<Promise> h) noexcept {
      initialFrame_ = &h.promise().getAsyncFrame();
      return false;
    }

    FOLLY_NOINLINE std::vector<std::uintptr_t> await_resume() {
      static constexpr size_t maxFrames = 100;
      std::array<std::uintptr_t, maxFrames> result;

      result[0] =
          reinterpret_cast<std::uintptr_t>(FOLLY_ASYNC_STACK_RETURN_ADDRESS());
      auto numFrames = getAsyncStackTraceFromInitialFrame(
          initialFrame_, result.data() + 1, maxFrames - 1);

      return std::vector<std::uintptr_t>(
          std::make_move_iterator(result.begin()),
          std::make_move_iterator(result.begin()) + numFrames + 1);
    }

   private:
    folly::AsyncStackFrame* initialFrame_;
  };

 public:
  AsyncStackTraceAwaitable viaIfAsync(
      const folly::Executor::KeepAlive<>&) const noexcept {
    return {};
  }

  Awaiter operator co_await() const noexcept { return {}; }

  friend AsyncStackTraceAwaitable tag_invoke(
      cpo_t<co_withAsyncStack>, AsyncStackTraceAwaitable awaitable) noexcept {
    return awaitable;
  }
};

inline constexpr AsyncStackTraceAwaitable co_current_async_stack_trace = {};

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
