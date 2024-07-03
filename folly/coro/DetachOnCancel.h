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
/**
 * detachOnCancel is used to handle operations that are hard to be cancelled. A
 * typical use case is: The caller starts a task with timeout (in this case, 1
 * sec timeout). The task itself launches a long running job and the job doesn't
 * handle cancellation (sleep_for in this example). The caller has timeout and
 * the cancellation is propagated to the task. The detachOnCancel detects the
 * cancellation and return immediately. However, the background task still runs
 * until the thread join.
 *
 * \refcode folly/docs/examples/folly/experimental/coro/DetachOnCancel.cpp
 *
 * It is important to manage the scope of each variable. If the long running
 * task references any variable that is created in the scope of detachOnCancel,
 * then the result may be freed and the long running task may trigger
 * use-after-free error.
 */
template <typename Awaitable>
Task<semi_await_result_t<Awaitable>> detachOnCancel(Awaitable awaitable) {
  auto posted = std::make_unique<std::atomic<bool>>(false);
  Baton baton;
  Try<detail::lift_lvalue_reference_t<semi_await_result_t<Awaitable>>> result;

  {
    auto t = co_invoke(
        [awaitable_2 = std::move(
             awaitable)]() mutable -> Task<semi_await_result_t<Awaitable>> {
          co_return co_await std::move(awaitable_2);
        });
    std::move(t)
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
  }

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
