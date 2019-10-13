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

#include <folly/CancellationToken.h>
#include <folly/experimental/coro/Baton.h>
#include <folly/experimental/coro/CurrentExecutor.h>

namespace folly {
namespace coro {

inline Task<void> sleep(Duration d, Timekeeper* tk) {
  folly::coro::Baton baton;
  auto future =
      folly::futures::sleep(d, tk).toUnsafeFuture().ensure([&]() noexcept {
        baton.post();
      });

  CancellationCallback cancelCallback(
      co_await co_current_cancellation_token, [&]() noexcept {
        future.cancel();
      });
  co_await baton;
}

} // namespace coro
} // namespace folly
