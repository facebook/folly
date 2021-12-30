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

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {

// Filter the Values from an input stream using an unary predicate.
//
// The input is a stream of Values.
//
// The output is a stream of Values that satisfy the predicate.
//
// Example:
//   AsyncGenerator<int> getAllNumbers();
//
//   AsyncGenerator<int> getEvenNumbers(AsyncGenerator<int> allNumbers) {
//     return filter(getAllNumbers(), [](int i){ return i % 2 == 0; });
//   }
template <typename FilterFn, typename Reference, typename Value>
AsyncGenerator<Reference, Value> filter(
    AsyncGenerator<Reference, Value> source, FilterFn filterFn);

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES

#include <folly/experimental/coro/Filter-inl.h>
