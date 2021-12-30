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

/*
 * This macro enables FbSystrace usage in production for fb4a. When
 * FOLLY_SCOPED_TRACE_SECTION_HEADER is defined then a trace section is started
 * and later automatically terminated at the close of the scope it is called in.
 * In all other cases no action is taken.
 */

#pragma once

#include <folly/Executor.h>
#include <folly/experimental/coro/Coroutine.h>
#include <folly/experimental/coro/WithAsyncStack.h>
#include <folly/tracing/AsyncStack.h>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {
namespace detail {

// Helper struct for getting access to the current coroutine's AsyncStackFrame
class CurrentAsyncStackFrameAwaitable {
  class Awaiter {
   public:
    bool await_ready() noexcept { return false; }

    template <typename Promise>
    bool await_suspend(coroutine_handle<Promise> h) noexcept {
      asyncFrame_ = &h.promise().getAsyncFrame();
      return false;
    }

    folly::AsyncStackFrame& await_resume() noexcept { return *asyncFrame_; }

   private:
    folly::AsyncStackFrame* asyncFrame_ = nullptr;
  };

 public:
  CurrentAsyncStackFrameAwaitable viaIfAsync(
      const folly::Executor::KeepAlive<>&) const noexcept {
    return {};
  }

  friend Awaiter tag_invoke(
      cpo_t<co_withAsyncStack>, CurrentAsyncStackFrameAwaitable) noexcept {
    return Awaiter{};
  }
};

// Await this object within a coroutine to obtain a reference to the current
// coroutine's AsyncStackFrame. This will only work within a coroutine whose
// promise_type implements the getAsyncFrame() method.
inline constexpr CurrentAsyncStackFrameAwaitable co_current_async_stack_frame{};

} // namespace detail
} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
