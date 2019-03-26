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
constexpr co_current_executor_t co_current_executor{
    co_current_executor_t::secret_::token_};

} // namespace coro
} // namespace folly
