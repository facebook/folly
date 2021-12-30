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

#include <folly/experimental/coro/AsyncGenerator.h>
#include <folly/experimental/coro/Coroutine.h>
#include <folly/experimental/coro/Task.h>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {

// Accumulate the values from an input stream into a single value given
// an optional binary accumulation operation, similar to std::accumulate.
//
// The input is a stream of values.
//
// The output is a Task containing the result of the accumulation
//
// Example:
//   AsyncGenerator<int> stream();
//
//   Task<void> consumer() {
//     auto sum = accumulate(stream(), 0, std::plus{});
//   }
template <typename Reference, typename Value, typename Output>
Task<Output> accumulate(
    AsyncGenerator<Reference, Value> generator, Output init);

template <
    typename Reference,
    typename Value,
    typename Output,
    typename BinaryOp>
Task<Output> accumulate(
    AsyncGenerator<Reference, Value> generator, Output init, BinaryOp op);

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES

#include <folly/experimental/coro/Accumulate-inl.h>
