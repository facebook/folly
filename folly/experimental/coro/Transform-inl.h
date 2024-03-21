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

#include <folly/Traits.h>
#include <folly/experimental/coro/Transform.h>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {

template <
    typename ReturnType,
    typename TransformFn,
    typename Reference,
    typename Value,
    typename ReturnReference>
AsyncGenerator<ReturnReference> transform(
    AsyncGenerator<Reference, Value> source, TransformFn transformFn) {
  while (auto item = co_await source.next()) {
    using InvokeResult = decltype(invoke(transformFn, std::move(item).value()));
    if constexpr (std::is_constructible_v<ReturnReference&&, InvokeResult>) {
      co_yield invoke(transformFn, std::move(item).value());
    } else {
      remove_cvref_t<ReturnReference> result =
          invoke(transformFn, std::move(item).value());
      co_yield std::forward<ReturnReference>(result);
    }
  }
}

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
