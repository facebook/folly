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

#include <utility>

#include <folly/Executor.h>
#include <folly/experimental/coro/ViaIfAsync.h>
#include <folly/io/async/Request.h>

namespace folly {
namespace coro {

namespace detail {
struct co_current_executor_ {
  enum class secret_ { token_ };
  explicit constexpr co_current_executor_(secret_) {}
};
} // namespace detail

using co_current_executor_t = detail::co_current_executor_;

// Special placeholder object that can be 'co_await'ed from within a Task<T>
// or an AsyncGenerator<T> to obtain the current folly::Executor associated
// with the current coroutine.
//
// Note that for a folly::Task the executor will remain the same throughout
// the lifetime of the coroutine. For a folly::AsyncGenerator<T> the current
// executor may change when resuming from a co_yield suspend-point.
//
// Example:
//   folly::coro::Task<void> example() {
//     Executor* e = co_await folly::coro::co_current_executor;
//     e->add([] { do_something(); });
//   }
inline constexpr co_current_executor_t co_current_executor{
    co_current_executor_t::secret_::token_};

namespace detail {

class co_reschedule_on_current_executor_ {
  class Awaiter {
    folly::Executor::KeepAlive<> executor_;

   public:
    explicit Awaiter(folly::Executor::KeepAlive<> executor) noexcept
        : executor_(std::move(executor)) {}

    bool await_ready() {
      return false;
    }

    FOLLY_CORO_AWAIT_SUSPEND_NONTRIVIAL_ATTRIBUTES void await_suspend(
        std::experimental::coroutine_handle<> coro) {
      executor_->add([coro, ctx = RequestContext::saveContext()]() mutable {
        RequestContextScopeGuard contextScope{std::move(ctx)};
        coro.resume();
      });
    }

    void await_resume() {}
  };

  friend Awaiter co_viaIfAsync(
      folly::Executor::KeepAlive<> executor,
      co_reschedule_on_current_executor_) {
    return Awaiter{std::move(executor)};
  }
};

} // namespace detail

using co_reschedule_on_current_executor_t =
    detail::co_reschedule_on_current_executor_;

// A SemiAwaitable object that allows you to reschedule the current coroutine
// onto the currently associated executor.
//
// This can be used as a form of cooperative multi-tasking for coroutines that
// wish to provide fair access to the execution resources. eg. to periodically
// give up their current execution slot to allow other tasks to run.
//
// Example:
//   folly::coro::Task<void> doCpuIntensiveWorkFairly() {
//     for (int i = 0; i < 1'000'000; ++i) {
//       // Periodically reschedule to the executor.
//       if ((i % 1024) == 1023) {
//         co_await folly::coro::co_reschedule_on_current_executor;
//       }
//       doSomeWork(i);
//     }
//   }
inline constexpr co_reschedule_on_current_executor_t
    co_reschedule_on_current_executor;

namespace detail {
struct co_current_cancellation_token_ {
  enum class secret_ { token_ };
  explicit constexpr co_current_cancellation_token_(secret_) {}
};
} // namespace detail

using co_current_cancellation_token_t = detail::co_current_cancellation_token_;

inline constexpr co_current_cancellation_token_t co_current_cancellation_token{
    co_current_cancellation_token_t::secret_::token_};

} // namespace coro
} // namespace folly
