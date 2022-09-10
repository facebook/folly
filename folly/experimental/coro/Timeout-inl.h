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

#include <folly/CancellationToken.h>
#include <folly/experimental/coro/Baton.h>
#include <folly/experimental/coro/WithCancellation.h>

#if FOLLY_HAS_COROUTINES

namespace folly::coro {

namespace detail {

template <bool>
struct DiscardImpl {
  folly::coro::Baton baton;
  exception_wrapper timeoutResult;
  bool parentCancelled = false;
  bool checkedTimeout = false;
};

template <>
struct DiscardImpl<false> {};

template <typename SemiAwaitable, typename Duration, bool discard>
Task<typename semi_await_try_result_t<SemiAwaitable>::element_type> timeoutImpl(
    SemiAwaitable semiAwaitable, Duration timeoutDuration, Timekeeper* tk) {
  CancellationSource cancelSource;
  DiscardImpl<discard> impl;
  auto sleepFuture =
      folly::futures::sleep(timeoutDuration, tk).toUnsafeFuture();
  sleepFuture.setCallback_(
      [&, cancelSource](Executor::KeepAlive<>&&, Try<Unit>&& result) noexcept {
        if constexpr (discard) {
          if (result.hasException()) {
            impl.timeoutResult = std::move(result.exception());
          } else {
            impl.timeoutResult = folly::make_exception_wrapper<FutureTimeout>();
          }
          impl.baton.post();
        }
        cancelSource.requestCancellation();
      });

  bool isSleepCancelled = false;
  auto tryCancelSleep = [&]() noexcept {
    if (!isSleepCancelled) {
      isSleepCancelled = true;
      sleepFuture.cancel();
    }
  };

  std::optional<CancellationCallback> cancelCallback{
      std::in_place, co_await co_current_cancellation_token, [&]() {
        cancelSource.requestCancellation();
        tryCancelSleep();
        if constexpr (discard) {
          impl.parentCancelled = true;
        }
      }};

  exception_wrapper error;
  try {
    auto resultTry =
        co_await folly::coro::co_awaitTry(folly::coro::co_withCancellation(
            cancelSource.getToken(), std::move(semiAwaitable)));

    cancelCallback.reset();

    if constexpr (discard) {
      if (!impl.parentCancelled && impl.baton.ready()) {
        // Timer already fired
        co_yield folly::coro::co_error(std::move(impl.timeoutResult));
      }
      impl.checkedTimeout = true;
    }

    tryCancelSleep();
    if constexpr (discard) {
      co_await impl.baton;
    }

    if (resultTry.hasException()) {
      co_yield folly::coro::co_error(std::move(resultTry).exception());
    }

    co_return std::move(resultTry).value();
  } catch (...) {
    error = exception_wrapper{std::current_exception()};
  }

  assert(error);

  cancelCallback.reset();

  if constexpr (discard) {
    if (!impl.checkedTimeout && !impl.parentCancelled && impl.baton.ready()) {
      // Timer already fired
      co_yield folly::coro::co_error(std::move(impl.timeoutResult));
    }
  }

  tryCancelSleep();
  if constexpr (discard) {
    co_await impl.baton;
  }

  co_yield folly::coro::co_error(std::move(error));
}

} // namespace detail

template <typename SemiAwaitable, typename Duration>
Task<typename semi_await_try_result_t<SemiAwaitable>::element_type> timeout(
    SemiAwaitable semiAwaitable, Duration timeoutDuration, Timekeeper* tk) {
  return detail::timeoutImpl<SemiAwaitable, Duration, /*discard=*/true>(
      std::move(semiAwaitable), timeoutDuration, tk);
}

template <typename SemiAwaitable, typename Duration>
Task<typename semi_await_try_result_t<SemiAwaitable>::element_type>
timeoutNoDiscard(
    SemiAwaitable semiAwaitable, Duration timeoutDuration, Timekeeper* tk) {
  return detail::timeoutImpl<SemiAwaitable, Duration, /*discard=*/false>(
      std::move(semiAwaitable), timeoutDuration, tk);
}

} // namespace folly::coro

#endif // FOLLY_HAS_COROUTINES
