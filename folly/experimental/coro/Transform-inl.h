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

#include <folly/experimental/coro/Transform.h>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {

template <typename TransformFn, typename Reference, typename Value>
AsyncGenerator<invoke_result_t<TransformFn&, Reference>> transform(
    AsyncGenerator<Reference, Value> source, TransformFn transformFn) {
  while (auto item = co_await source.next()) {
    co_yield invoke(transformFn, std::move(item).value());
  }
}

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
