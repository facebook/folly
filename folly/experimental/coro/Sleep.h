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
#include <folly/futures/Future.h>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {

/// Return a task that, when awaited, will sleep for the specified duration.
///
/// Throws folly::OperationCancelled if cancellation is requested on the
/// awaiting coroutine's associated CancellationToken.
Task<void> sleep(HighResDuration d, Timekeeper* tk = nullptr);

/// Return a task that, when awaited, will sleep for the specified duration.
///
/// May complete sooner that the specified duration if cancellation is requested
/// on the awaiting coroutine's associated CancellationToken.
Task<void> sleepReturnEarlyOnCancel(
    HighResDuration d, Timekeeper* tk = nullptr);

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES

#include <folly/experimental/coro/Sleep-inl.h>
