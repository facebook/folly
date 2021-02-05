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

#include <type_traits>

#include <folly/experimental/coro/Baton.h>
#include <folly/experimental/coro/Coroutine.h>
#include <folly/experimental/coro/Invoke.h>
#include <folly/experimental/coro/Task.h>
#include <folly/experimental/coro/Traits.h>
#include <folly/experimental/coro/detail/Helpers.h>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {
template <typename Awaitable>
Task<semi_await_result_t<Awaitable>> detachOnCancel(Awaitable awaitable) {
  auto posted = std::make_unique<std::atomic<bool>>(false);
  Baton baton;
  Try<detail::lift_lvalue_reference_t<semi_await_result_t<Awaitable>>> result;

  co_invoke(
      [awaitable = std::move(
           awaitable)]() mutable -> Task<semi_await_result_t<Awaitable>> {
        co_return co_await std::move(awaitable);
      })
      .scheduleOn(co_await co_current_executor)
      .startInlineUnsafe(
          [postedPtr = posted.get(), &baton, &result](auto&& r) {
            std::unique_ptr<std::atomic<bool>> p(postedPtr);
            if (!p->exchange(true, std::memory_order_relaxed)) {
              p.release();
              tryAssign(result, std::move(r));
              baton.post();
            }
          },
          co_await co_current_cancellation_token);
  {
    CancellationCallback cancelCallback(
        co_await co_current_cancellation_token, [&posted, &baton, &result] {
          if (!posted->exchange(true, std::memory_order_relaxed)) {
            posted.release();
            result.emplaceException(folly::OperationCancelled{});
            baton.post();
          }
        });
    co_await baton;
  }

  if (result.hasException()) {
    co_yield folly::coro::co_error(result.exception());
  }

  co_return std::move(result).value();
}
} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
