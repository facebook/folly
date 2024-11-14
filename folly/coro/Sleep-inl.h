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

#include <folly/coro/FutureUtil.h>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {

inline Task<void> sleep(HighResDuration d, Timekeeper* tk) {
  //  using via with the current executor is observed to deadlock in some cases,
  //  so convert to future without via and thereby bypass conversion using via
  //  in the overload of toTaskInterruptOnCancel taking semi-future; woroks only
  //  since sleep() returns a semi-future without any deferred work attached
  auto f = folly::futures::sleep(d, tk).toUnsafeFuture();
  co_await co_nothrow(toTaskInterruptOnCancel(std::move(f)));
}

inline Task<void> sleepReturnEarlyOnCancel(HighResDuration d, Timekeeper* tk) {
  auto result = co_await co_awaitTry(sleep(d, tk));
  if (result.hasException<OperationCancelled>()) {
    co_return;
  }
  co_yield co_result(std::move(result));
}

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
