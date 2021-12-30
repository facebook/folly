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

#include <cstdint>

#include <folly/CancellationToken.h>
#include <folly/experimental/coro/Baton.h>
#include <folly/experimental/coro/Task.h>

namespace folly {
namespace python {
namespace test {

folly::coro::Task<uint64_t> coro_getValueX5(uint64_t val) {
  if (val == 0) {
    throw std::invalid_argument("0 is not allowed");
  }
  co_return val * 5;
}

folly::coro::Task<uint64_t> coro_returnFiveAfterCancelled() {
  folly::coro::Baton baton;
  const folly::CancellationToken& ct =
      co_await folly::coro::co_current_cancellation_token;
  folly::CancellationCallback cb{ct, [&] { baton.post(); }};
  co_await baton;
  co_return 5;
}
} // namespace test
} // namespace python
} // namespace folly
