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

#include <folly/Executor.h>
#include <folly/experimental/coro/Coroutine.h>
#include <folly/experimental/coro/ViaIfAsync.h>
#include <folly/experimental/coro/WithAsyncStack.h>
#include <folly/io/async/Request.h>

#if FOLLY_HAS_COROUTINES

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
  class AwaiterBase {
   public:
    explicit AwaiterBase(folly::Executor::KeepAlive<> executor) noexcept
        : executor_(std::move(executor)) {}

    bool await_ready() noexcept { return false; }

    void await_resume() noexcept {}

   protected:
    folly::Executor::KeepAlive<> executor_;
  };

 public:
  class StackAwareAwaiter : public AwaiterBase {
   public:
    using AwaiterBase::AwaiterBase;

    template <typename Promise>
    void await_suspend(coroutine_handle<Promise> coro) noexcept {
      await_suspend_impl(coro, coro.promise().getAsyncFrame());
    }

   private:
    FOLLY_CORO_AWAIT_SUSPEND_NONTRIVIAL_ATTRIBUTES void await_suspend_impl(
        coroutine_handle<> coro, AsyncStackFrame& frame) {
      auto& stackRoot = *frame.getStackRoot();
      folly::deactivateAsyncStackFrame(frame);
      try {
        executor_->add(
            [coro, &frame, ctx = RequestContext::saveContext()]() mutable {
              RequestContextScopeGuard contextScope{std::move(ctx)};
              folly::resumeCoroutineWithNewAsyncStackRoot(coro, frame);
            });
      } catch (...) {
        folly::activateAsyncStackFrame(stackRoot, frame);
        throw;
      }
    }
  };

  class Awaiter : public AwaiterBase {
   public:
    using AwaiterBase::AwaiterBase;

    FOLLY_CORO_AWAIT_SUSPEND_NONTRIVIAL_ATTRIBUTES void await_suspend(
        coroutine_handle<> coro) {
      executor_->add([coro, ctx = RequestContext::saveContext()]() mutable {
        RequestContextScopeGuard contextScope{std::move(ctx)};
        coro.resume();
      });
    }

    friend StackAwareAwaiter tag_invoke(
        cpo_t<co_withAsyncStack>, Awaiter awaiter) {
      return StackAwareAwaiter{std::move(awaiter.executor_)};
    }
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

//  co_safe_point_t
//  co_safe_point
//
//  A semi-awaitable type and value which, when awaited in an async coroutine
//  supporting safe-points, causes a safe-point to be reached.
//
//  Example:
//
//    co_await co_safe_point; // a safe-point is reached
//
//  At this safe-point:
//  - If cancellation has been requested then the coroutine is terminated with
//    cancellation.
//  - To aid overall system concurrency, the coroutine may be rescheduled onto
//    the current executor.
//  - Otherwise, the coroutine is resumed.
//
//  Recommended for use wherever cancellation is checked and handled via early
//  termination.
//
//  Technical note: behavior is typically implemented in some overload
//  of await_transform in the coroutine's promise type, or in the awaitable
//  or awaiter it returns. Example:
//
//      struct /* some coroutine type */ {
//        struct promise_type {
//          /* some awaiter */ await_transform(co_safe_point_t) noexcept;
//        };
//      };
class co_safe_point_t final {};
FOLLY_INLINE_VARIABLE constexpr co_safe_point_t co_safe_point{};

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
