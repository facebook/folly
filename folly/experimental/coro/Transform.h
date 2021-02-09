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

#include <folly/experimental/coro/AsyncGenerator.h>
#include <folly/experimental/coro/Coroutine.h>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {

// Transform the Values from an input stream into a stream of the
// Trandformed Values.
//
// The input is a stream of Values.
//
// The output is a stream of Transformed Value.
//
// Example:
//   AsyncGenerator<int> stream();
//
//   Task<void> consumer() {
//     auto events = transform(stream(), [](int i){ return i * 1.0f; });
//     try {
//       while (auto item = co_await events.next()) {
//         // Value
//         float value = *item;
//         std::cout << "value " << value << "\n";
//       }
//       // End Of Stream
//       std::cout << "end\n";
//     } catch (const std::exception& error) {
//       // Exception
//       std::cout << "error " << error.what() << "\n";
//     }
//   }
template <typename TransformFn, typename Reference, typename Value>
AsyncGenerator<invoke_result_t<TransformFn&, Reference>> transform(
    AsyncGenerator<Reference, Value> source, TransformFn transformFn);

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES

#include <folly/experimental/coro/Transform-inl.h>
