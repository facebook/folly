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

#include <folly/Optional.h>
#include <folly/experimental/coro/Baton.h>
#include <folly/experimental/coro/Coroutine.h>
#include <folly/experimental/coro/Invoke.h>
#include <folly/experimental/coro/Task.h>
#include <folly/experimental/coro/Traits.h>
#include <folly/experimental/coro/detail/Helpers.h>
#include <folly/futures/Future.h>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {
template <typename Awaitable>
Task<Optional<lift_unit_t<detail::decay_rvalue_reference_t<
    detail::lift_lvalue_reference_t<semi_await_result_t<Awaitable>>>>>>
timed_wait(Awaitable awaitable, Duration duration) {
  Baton baton;
  Try<lift_unit_t<detail::decay_rvalue_reference_t<
      detail::lift_lvalue_reference_t<semi_await_result_t<Awaitable>>>>>
      result;

  auto sleepFuture = futures::sleep(duration).toUnsafeFuture();
  auto posted = new std::atomic<bool>(false);
  std::move(sleepFuture)
      .setCallback_([posted, &baton, executor = co_await co_current_executor](
                        auto&&, auto&&) {
        if (!posted->exchange(true, std::memory_order_acq_rel)) {
          executor->add([&baton] { baton.post(); });
        } else {
          delete posted;
        }
      });

  {
    auto t = co_invoke(
        [awaitable = std::move(
             awaitable)]() mutable -> Task<semi_await_result_t<Awaitable>> {
          co_return co_await std::move(awaitable);
        });
    std::move(t)
        .scheduleOn(co_await co_current_executor)
        .start([posted, &baton, &result](auto&& r) {
          if (!posted->exchange(true, std::memory_order_acq_rel)) {
            result = std::move(r);
            baton.post();
          } else {
            delete posted;
          }
        });
  }

  co_await detail::UnsafeResumeInlineSemiAwaitable{get_awaiter(baton)};

  if (!result.hasValue() && !result.hasException()) {
    co_return folly::none;
  }
  co_return std::move(*result);
}

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
