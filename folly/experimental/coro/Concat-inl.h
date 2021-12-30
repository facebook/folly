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

#include <folly/experimental/coro/Coroutine.h>

namespace folly {
namespace coro {

template <
    typename HReference,
    typename... TReference,
    typename HValue,
    typename... TValue>
AsyncGenerator<HReference, HValue> concat(
    AsyncGenerator<HReference, HValue> head,
    AsyncGenerator<TReference, TValue>... tail) {
  static_assert((std::is_same_v<decltype(head), decltype(tail)> && ...));
  using list = AsyncGenerator<HReference, HValue>[];
  for (auto& gen : list{std::move(head), std::move(tail)...}) {
    while (auto val = co_await gen.next()) {
      co_yield std::move(val).value();
    }
  }
}

} // namespace coro
} // namespace folly
