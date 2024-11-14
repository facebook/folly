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

#include <folly/CancellationToken.h>
#include <folly/coro/Baton.h>
#include <folly/coro/Coroutine.h>
#include <folly/coro/CurrentExecutor.h>
#include <folly/coro/Invoke.h>
#include <folly/coro/Task.h>
#include <folly/coro/Traits.h>
#include <folly/futures/Future.h>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {

// Converts the given SemiAwaitable to a Task (without starting it)
template <typename SemiAwaitable>
Task<semi_await_result_t<SemiAwaitable>> toTask(SemiAwaitable&& a) {
  return co_invoke(
      [a = std::forward<SemiAwaitable>(a)]() mutable //
      -> Task<semi_await_result_t<SemiAwaitable>> {
        co_return co_await std::forward<SemiAwaitable>(a);
      });
}
template <typename SemiAwaitable>
Task<semi_await_result_t<SemiAwaitable>> toTask(
    std::reference_wrapper<SemiAwaitable> a) {
  co_return co_await a.get();
}
inline Task<void> toTask(folly::Future<Unit> a) {
  co_yield co_result(co_await co_awaitTry(std::move(a)));
}
inline Task<void> toTask(folly::SemiFuture<Unit> a) {
  co_yield co_result(co_await co_awaitTry(std::move(a)));
}

template <typename V>
Task<drop_unit_t<V>> toTaskInterruptOnCancel(folly::Future<V> f) {
  bool cancelled{false};
  Baton baton;
  Try<V> result;
  f.setCallback_(
      [&result, &baton](Executor::KeepAlive<>&&, Try<V>&& t) {
        result = std::move(t);
        baton.post();
      },
      // No user logic runs in the callback, we can avoid the cost of switching
      // the context.
      /* context */ nullptr);

  {
    CancellationCallback cancelCallback(
        co_await co_current_cancellation_token, [&]() noexcept {
          cancelled = true;
          f.cancel();
        });
    co_await baton;
  }
  if (cancelled) {
    co_yield co_cancelled;
  }
  co_yield co_result(std::move(result));
}

template <typename V>
Task<drop_unit_t<V>> toTaskInterruptOnCancel(folly::SemiFuture<V> f) {
  auto ex = co_await co_current_executor;
  co_await co_nothrow(toTaskInterruptOnCancel(std::move(f).via(ex)));
}

// Converts the given SemiAwaitable to a SemiFuture (without starting it)
template <typename SemiAwaitable>
folly::SemiFuture<
    lift_unit_t<semi_await_result_t<remove_reference_wrapper_t<SemiAwaitable>>>>
toSemiFuture(SemiAwaitable&& a) {
  return toTask(std::forward<SemiAwaitable>(a)).semi();
}

// Converts the given SemiAwaitable to a Future, starting it on the Executor
template <typename SemiAwaitable>
folly::Future<
    lift_unit_t<semi_await_result_t<remove_reference_wrapper_t<SemiAwaitable>>>>
toFuture(SemiAwaitable&& a, Executor::KeepAlive<> ex) {
  auto excopy = ex;
  return toTask(std::forward<SemiAwaitable>(a))
      .scheduleOn(std::move(excopy))
      .start()
      .via(std::move(ex));
}

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
