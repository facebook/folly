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

#include <folly/experimental/coro/FutureUtil.h>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {

inline Task<void> sleep(HighResDuration d, Timekeeper* tk) {
  co_await co_nothrow(toTaskInterruptOnCancel(folly::futures::sleep(d, tk)));
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
