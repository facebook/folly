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

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {

template <typename Reference, typename Value, typename Output>
Task<Output> accumulate(
    AsyncGenerator<Reference, Value> generator, Output init) {
  return accumulate(std::move(generator), std::move(init), std::plus{});
}

template <
    typename Reference,
    typename Value,
    typename Output,
    typename BinaryOp>
Task<Output> accumulate(
    AsyncGenerator<Reference, Value> generator, Output init, BinaryOp op) {
  while (auto next = co_await generator.next()) {
    init = op(std::move(init), std::move(next).value());
  }
  co_return init;
}
} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
