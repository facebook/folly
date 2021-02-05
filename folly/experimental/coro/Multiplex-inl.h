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

#include <folly/experimental/coro/Materialize.h>
#include <folly/experimental/coro/Merge.h>
#include <folly/experimental/coro/Transform.h>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {

template <
    typename SelectIdFn,
    typename Reference,
    typename Value,
    typename KeyType>
AsyncGenerator<
    Enumerated<KeyType, CallbackRecord<Reference>>,
    Enumerated<KeyType, CallbackRecord<Value>>>
multiplex(
    folly::Executor::KeepAlive<> exec,
    AsyncGenerator<AsyncGenerator<Reference, Value>>&& sources,
    SelectIdFn&& selectId) {
  using EventType = CallbackRecord<Reference>;
  using ReferenceType = Enumerated<KeyType, EventType>;

  return merge(
      std::move(exec),
      transform(
          std::move(sources),
          [selectId = std::forward<SelectIdFn>(selectId)](
              AsyncGenerator<Reference, Value>&& item) mutable {
            KeyType id = invoke(selectId, item);
            return transform(
                materialize(std::move(item)),
                [id = std::move(id)](EventType&& event) {
                  return ReferenceType{id, std::move(event)};
                });
          }));
}

struct MultiplexIdcountFn {
  size_t n = 0;
  template <typename Inner>
  size_t operator()(Inner&&) noexcept {
    return n++;
  }
};

template <typename Reference, typename Value>
AsyncGenerator<
    Enumerated<size_t, CallbackRecord<Reference>>,
    Enumerated<size_t, CallbackRecord<Value>>>
multiplex(
    folly::Executor::KeepAlive<> exec,
    AsyncGenerator<AsyncGenerator<Reference, Value>>&& sources) {
  return multiplex(std::move(exec), std::move(sources), MultiplexIdcountFn{});
}

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
