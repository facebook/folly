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

#include <folly/CancellationToken.h>
#include <folly/experimental/coro/Baton.h>
#include <folly/experimental/coro/WithCancellation.h>

#if FOLLY_HAS_COROUTINES

namespace folly::coro {

template <typename SemiAwaitable, typename Duration>
Task<typename semi_await_try_result_t<SemiAwaitable>::element_type> timeout(
    SemiAwaitable semiAwaitable, Duration timeoutDuration, Timekeeper* tk) {
  CancellationSource cancelSource;
  folly::coro::Baton baton;
  exception_wrapper timeoutResult;
  auto sleepFuture =
      folly::futures::sleep(timeoutDuration, tk).toUnsafeFuture();
  sleepFuture.setCallback_(
      [&](Executor::KeepAlive<>&&, Try<Unit>&& result) noexcept {
        if (result.hasException()) {
          timeoutResult = std::move(result.exception());
        } else {
          timeoutResult = folly::make_exception_wrapper<FutureTimeout>();
        }
        cancelSource.requestCancellation();
        baton.post();
      });

  bool isSleepCancelled = false;
  auto tryCancelSleep = [&]() noexcept {
    if (!isSleepCancelled) {
      isSleepCancelled = true;
      sleepFuture.cancel();
    }
  };

  bool parentCancelled = false;
  std::optional<CancellationCallback> cancelCallback{
      std::in_place, co_await co_current_cancellation_token, [&]() {
        cancelSource.requestCancellation();
        tryCancelSleep();
        parentCancelled = true;
      }};

  bool checkedTimeout = false;

  exception_wrapper error;
  try {
    auto resultTry =
        co_await folly::coro::co_awaitTry(folly::coro::co_withCancellation(
            cancelSource.getToken(), std::move(semiAwaitable)));

    cancelCallback.reset();

    if (!parentCancelled && baton.ready()) {
      // Timer already fired
      co_yield folly::coro::co_error(std::move(timeoutResult));
    }

    checkedTimeout = true;

    tryCancelSleep();
    co_await baton;

    if (resultTry.hasException()) {
      co_yield folly::coro::co_error(std::move(resultTry).exception());
    }

    co_return std::move(resultTry).value();
  } catch (...) {
    error = exception_wrapper{std::current_exception()};
  }

  assert(error);

  cancelCallback.reset();

  if (!checkedTimeout && !parentCancelled && baton.ready()) {
    // Timer already fired
    co_yield folly::coro::co_error(std::move(timeoutResult));
  }

  tryCancelSleep();
  co_await baton;

  co_yield folly::coro::co_error(std::move(error));
}

} // namespace folly::coro

#endif // FOLLY_HAS_COROUTINES
