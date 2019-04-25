/*
 * Copyright 2019-present Facebook, Inc.
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

#include <folly/Executor.h>
#include <folly/io/async/Request.h>

#include <experimental/coroutine>

namespace folly {
namespace coro {

namespace detail {

class co_schedule_t {
  class Awaiter {
    folly::Executor::KeepAlive<> executor_;

   public:
    explicit Awaiter(folly::Executor::KeepAlive<> executor) noexcept
        : executor_(std::move(executor)) {}

    bool await_ready() {
      return false;
    }

    void await_suspend(std::experimental::coroutine_handle<> coro) {
      executor_->add([coro, ctx = RequestContext::saveContext()]() mutable {
        RequestContextScopeGuard contextScope{std::move(ctx)};
        coro.resume();
      });
    }

    void await_resume() {}
  };

  friend Awaiter co_viaIfAsync(
      folly::Executor::KeepAlive<> executor,
      co_schedule_t) {
    return Awaiter{std::move(executor)};
  }
};

} // namespace detail

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
//         co_await folly::coro::co_schedule;
//       }
//       doSomeWork(i);
//     }
//   }
inline constexpr detail::co_schedule_t co_schedule;

} // namespace coro
} // namespace folly
