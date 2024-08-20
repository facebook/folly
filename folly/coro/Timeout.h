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

#include <folly/experimental/coro/Coroutine.h>
#include <folly/experimental/coro/Task.h>
#include <folly/experimental/coro/Traits.h>
#include <folly/futures/Future.h>

#if FOLLY_HAS_COROUTINES

namespace folly::coro {

/// Returns a Task that, when started, starts a timer of duration
/// 'timeoutDuration' and awaits the passed SemiAwaitable.
///
/// If the timeoutDuration elapses before the 'co_await semiAwaitable'
/// operation completes then requests cancellation of the child operation
/// and completes with an error of type folly::FutureTimeout.
/// Otherwise, if the 'co_await semiAwaitable' operation completes before
/// the timeoutDuration elapses then cancels the timer and completes with
/// the result of the semiAwaitable.
///
/// IMPORTANT: The operation passed as the first argument must be able
/// to respond to a request for cancellation on the CancellationToken
/// injected to it via folly::coro::co_withCancellation in a timely manner for
/// the timeout to work as expected.
///
/// If a timekeeper is provided then uses that timekeeper to start the timer,
/// otherwise uses the process' default TimeKeeper if 'tk' is null.
///
/// \throws folly::FutureTimeout
/// \refcode folly/docs/examples/folly/experimental/coro/DetachOnCancel.cpp
template <typename SemiAwaitable, typename Duration>
Task<typename semi_await_try_result_t<SemiAwaitable>::element_type> timeout(
    SemiAwaitable semiAwaitable,
    Duration timeoutDuration,
    Timekeeper* tk = nullptr);

/// Returns a Task that, when started, starts a timer of duration
/// 'timeoutDuration' and awaits the passed SemiAwaitable (operation).
///
/// The returned result is *always* that of the operation. In other words the
/// result is never discarded, in contrast with `timeout`.
///
/// If the timeout duration elapses before the operation completes, the result
/// should and typically will reflect cancellation (e.g. `OperationCancelled`)
/// but this depends on how the operation responds (as cancellation is
/// cooperative).
///
/// To disambiguate between cancellation and timeout, callers can inspect their
/// own cancellation token.
///
/// IMPORTANT: This function has no effect if the passed operation does not
/// respond to cancellation. The operation passed as the first argument must be
/// able to respond to a request for cancellation on the CancellationToken
/// injected to it via folly::coro::co_withCancellation in a timely manner for
/// the timeout to work as expected.
///
/// If a timekeeper is provided then uses that timekeeper to start the timer,
/// otherwise uses the process' default TimeKeeper if 'tk' is null.
template <typename SemiAwaitable, typename Duration>
Task<typename semi_await_try_result_t<SemiAwaitable>::element_type>
timeoutNoDiscard(
    SemiAwaitable semiAwaitable,
    Duration timeoutDuration,
    Timekeeper* tk = nullptr);

} // namespace folly::coro

#endif // FOLLY_HAS_COROUTINES

#include <folly/coro/Timeout-inl.h>
